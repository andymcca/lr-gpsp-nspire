/* arm926.c: a QEMU TCG plugin that estimates how many cycles an ARM program
 * would take on the TI-Nspire CX II's ARM926EJ-S. tests/calc_speed.sh runs
 * gpSP's speed harness (tests/arm/perf.c, the core with its dynarec, the
 * renderer and the picture output, built like the calculator build) under
 * qemu-arm with this plugin. It started as PocketSNES's plugin of the same
 * name (tools/calc_speed in pocketsnes-nspire), with these changes for gpSP:
 * code written while it runs (the dynarec's) is decoded again when it
 * changes, cycles are split into regions (a function and everything it
 * calls, such as the renderer), and the memory timings are the ones
 * measured on the calculator.
 *
 * The model:
 *  - Pipeline: 1 cycle per instruction, plus the ARM926EJ-S's extra cycles
 *    for taken branches and other writes to the PC, LDM/STM (a cycle per
 *    register), multiplies, register-specified shifts, and a load's result
 *    used by the next instruction (or the one after, for bytes and halves).
 *  - Caches: 16 KB instruction and 8 KB data cache (what the CX II's cache
 *    type register reports), 4-way, 32-byte lines, round-robin replacement;
 *    the data cache is write-back and allocates on reads only.
 *  - Memory: one bus. A cache miss waits for the write buffer to empty, then
 *    takes "fill" cycles; a store that misses the cache goes into the write
 *    buffer (4 entries; the words of one STM share an entry), which writes
 *    one entry per "write" cycles (plus "word" per extra word) in the
 *    background and stalls the CPU only when full; a dirty line thrown out
 *    of the cache keeps the bus busy for "writeback" cycles.
 * Measured on the calculator (PocketSNES's FINDINGS.md, 395 MHz): a line
 * read from memory costs about 120 cycles; stores that miss go out at about
 * 6 cycles a word, single or in bursts. Those are the defaults.
 *
 * Arguments (-plugin arm926.so,arg=value,...):
 *   out=FILE      results (default arm926.out)
 *   mark=ADDR     address (hex) of a function called at the start of every
 *                 frame: only cycles after its first call count, and the
 *                 cycles between calls are listed per frame
 *   first=N last=M  list the instructions of frames N+1 to M only (frame
 *                 1 starts at the first mark; the frame lines list them all)
 *   regions=FILE  lines "ADDR NAME": the cycles of a call to the function at
 *                 ADDR, and of everything it calls, go to NAME (up to 7
 *                 names; the rest go to region 0, "cpu")
 *   fill=N write=N word=N writeback=N    memory timings (cycles)
 *   repl=rr|counter|random   which line a fill replaces (see p_repl)
 *   icache=BYTES dcache=BYTES   other cache sizes, to see what a part needs
 * The output lists the totals, the cycles and instructions of every region
 * in every frame, and the instructions and cycles of every instruction
 * address per region (for per-function sums by tests/calc_speed.sh), with
 * the cycles it waited for instruction fetches, data and the write buffer
 * (all regions together, on its first line). */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t p_fill = 120, p_write = 6, p_word = 6, p_writeback = 48;
static uint64_t mark_addr;
static const char *out_path = "arm926.out";
static uint64_t first_frame, last_frame = ~(uint64_t) 0;	/* frames whose instructions are listed */
static uint64_t frame_index;
static int listing = 1;

#define MAX_REGIONS 8
static const char *region_names[MAX_REGIONS] = { "cpu" };
static int n_regions = 1;
static GHashTable *region_entry;	/* entry address -> region index + 1 */

/* What the timing needs to know about an instruction */
struct insn
{
    uint64_t pc;
    uint32_t word;
    uint32_t reads;	/* registers the instruction reads (bit n = rn) */
    uint8_t base;	/* cycles when it runs and nothing stalls */
    uint8_t taken;	/* extra cycles if the next instruction isn't pc + 4 */
    uint8_t load_rd;	/* register a load writes (0xff: none) */
    uint8_t load_lat;	/* cycles its result isn't ready for the next instruction */
    uint8_t thumb;
    uint8_t region;	/* a region starts here (index + 1), or 0 */
    uint64_t count[MAX_REGIONS], cycles[MAX_REGIONS];
    uint64_t istall, dstall, wstall;	/* cycles waiting for fetches, data, the write buffer */
};

/* State, all on one vCPU */
static uint64_t now;			/* the cycle counter */
static uint64_t bus_free;		/* the memory bus is busy until then */
static uint64_t wb_done[4];		/* when each write buffer entry is written */
static int wb_count;			/* entries in the buffer (wb_done[0] oldest) */
static uint64_t wb_last_addr;		/* the newest entry's last word, for STM bursts */
static int wb_last_words;
static struct insn *wb_last_insn;
static struct insn *prev;		/* the instruction before this one */
static int prev_loaded;			/* it was a load and it ran (its condition passed) */
static uint8_t pending_rd = 0xff;	/* a byte/half load two instructions back */
static int counting;			/* after the first frame mark */
static GArray *frame_marks;		/* per frame mark: cycles and instructions of each region */
static uint64_t region_cycles[MAX_REGIONS], region_insns[MAX_REGIONS];
static uint64_t stat_insns, stat_imiss, stat_dmiss, stat_smiss, stat_wbstall, stat_fill_wait;
static uint64_t stat_cycles_start;
static GPtrArray *all_insns;
static GHashTable *by_pc_word;

/* The regions being run: return address and region of each call */
#define STACK_DEPTH 64
static struct
{
    uint64_t ret;
    int region;
} stack[STACK_DEPTH];
static int depth;
static int cur_region;

static struct qemu_plugin_register *reg_lr;

/* Caches: tags per set and way, round-robin victim per set */
#define LINE_SHIFT 5
struct cache
{
    int sets, ways;
    uint64_t *tag;	/* sets * ways; ~0 = empty */
    uint8_t *dirty;
    uint8_t *victim;
};

static struct cache icache, dcache;
/* Victim choice: 0 round-robin per set (the default), 1 one round-robin
 * counter per cache (stepped at each line fill), 2 pseudo-random. Which one
 * the calculator uses (CP15 control register bit 14, random after reset)
 * isn't known, so the speed estimate can be run with each (repl=). */
static int p_repl;
static int icache_bytes = 16 * 1024, dcache_bytes = 8 * 1024;

static void cache_init(struct cache *c, int bytes, int ways)
{
    c->ways = ways;
    c->sets = (bytes >> LINE_SHIFT) / ways;
    c->tag = g_new(uint64_t, c->sets * ways);
    c->dirty = g_new0(uint8_t, c->sets * ways);
    c->victim = g_new0(uint8_t, c->sets);
    for (int i = 0; i < c->sets * ways; i++)
	c->tag[i] = ~(uint64_t) 0;
}

/* Returns the way holding the line, or -1 */
static int cache_find(struct cache *c, uint64_t addr, int *set_out)
{
    uint64_t line = addr >> LINE_SHIFT;
    int set = line % c->sets;
    *set_out = set;
    for (int w = 0; w < c->ways; w++)
	if (c->tag[set * c->ways + w] == line)
	    return w;
    return -1;
}

/* Puts the line in; returns 1 if a dirty line was thrown out */
static int cache_fill(struct cache *c, uint64_t addr, int set)
{
    static uint32_t lfsr = 0x12345678;
    int w;
    if (p_repl == 2)
    {
	lfsr ^= lfsr << 13;
	lfsr ^= lfsr >> 17;
	lfsr ^= lfsr << 5;
	w = lfsr % c->ways;
    }
    else if (p_repl == 1)
    {
	w = c->victim[0];
	c->victim[0] = (w + 1) % c->ways;
    }
    else
    {
	w = c->victim[set];
	c->victim[set] = (w + 1) % c->ways;
    }
    int was_dirty = c->dirty[set * c->ways + w] && c->tag[set * c->ways + w] != ~(uint64_t) 0;
    c->tag[set * c->ways + w] = addr >> LINE_SHIFT;
    c->dirty[set * c->ways + w] = 0;
    return was_dirty;
}

static void spend(struct insn *in, uint64_t cycles)
{
    if (listing)
	in->cycles[cur_region] += cycles;
    region_cycles[cur_region] += cycles;
    now += cycles;
}

static void stall_to(uint64_t t, struct insn *in)
{
    if (t > now)
	spend(in, t - now);
}

static void wb_retire(void)
{
    int k = 0;
    while (k < wb_count && wb_done[k] <= now)
	k++;
    if (k)
    {
	memmove(wb_done, wb_done + k, (wb_count - k) * sizeof wb_done[0]);
	wb_count -= k;
    }
}

/* A line fill (instruction or data read miss): the write buffer is emptied
 * first, then the line comes in. */
static uint64_t line_fill(struct insn *in)
{
    uint64_t start = now, before = now;
    if (wb_count && wb_done[wb_count - 1] > start)
	start = wb_done[wb_count - 1];
    if (bus_free > start)
	start = bus_free;
    stat_fill_wait += start - now;
    stall_to(start + p_fill, in);
    bus_free = now;
    wb_count = 0;
    return now - before;
}

static void store_miss(struct insn *in, uint64_t addr)
{
    wb_retire();
    /* The next word of the same STM joins the newest entry */
    if (wb_count && wb_last_insn == in && addr == wb_last_addr + 4 && wb_last_words < 8 &&
	wb_done[wb_count - 1] > now)
    {
	wb_done[wb_count - 1] += p_word;
	if (bus_free < wb_done[wb_count - 1])
	    bus_free = wb_done[wb_count - 1];
	wb_last_addr = addr;
	wb_last_words++;
	return;
    }
    if (wb_count == 4)
    {
	uint64_t before = now;
	stall_to(wb_done[0], in);
	stat_wbstall += now - before;
	if (listing)
	    in->wstall += now - before;
	wb_retire();
    }
    uint64_t start = now;
    if (wb_count && wb_done[wb_count - 1] > start)
	start = wb_done[wb_count - 1];
    if (bus_free > start)
	start = bus_free;
    wb_done[wb_count++] = start + p_write;
    bus_free = start + p_write;
    wb_last_insn = in;
    wb_last_addr = addr;
    wb_last_words = 1;
}

static void vcpu_mem(unsigned int vcpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void *udata)
{
    struct insn *in = udata;
    int set, way;
    if (!counting)
	return;
    way = cache_find(&dcache, vaddr, &set);
    if (qemu_plugin_mem_is_store(info))
    {
	if (way >= 0)
	    dcache.dirty[set * dcache.ways + way] = 1;
	else
	{
	    stat_smiss++;
	    store_miss(in, vaddr);
	}
    }
    else
    {
	prev_loaded = 1;
	if (way < 0)
	{
	    stat_dmiss++;
	{
	    uint64_t w = line_fill(in);
	    if (listing)
		in->dstall += w;
	}
	    if (cache_fill(&dcache, vaddr, set))
		bus_free += p_writeback;
	}
    }
}

static void frame_mark(void)
{
    frame_index++;
    listing = frame_index > first_frame && frame_index <= last_frame;
    if (!counting)
    {
	counting = 1;
	stat_cycles_start = now;
	memset(region_cycles, 0, sizeof region_cycles);
	memset(region_insns, 0, sizeof region_insns);
    }
    g_array_append_vals(frame_marks, region_cycles, MAX_REGIONS);
    g_array_append_vals(frame_marks, region_insns, MAX_REGIONS);
}

static void run_insn(struct insn *in)
{
    /* Back in the caller of a region's function */
    while (depth && in->pc == stack[depth - 1].ret)
    {
	depth--;
	cur_region = depth ? stack[depth - 1].region : 0;
    }
    if (in->pc == mark_addr)
	frame_mark();
    if (!counting)
	return;

    if (prev)
    {
	/* A taken branch or other jump: the pipeline refills */
	if (prev->taken && in->pc != prev->pc + (prev->thumb ? 2 : 4))
	    spend(prev, prev->taken);
	/* A load's result needed right away */
	if (prev_loaded && prev->load_rd != 0xff && (in->reads >> prev->load_rd & 1))
	    spend(in, prev->load_lat);
	else if (pending_rd != 0xff && (in->reads >> pending_rd & 1))
	    spend(in, 1);
	pending_rd = prev_loaded && prev->load_lat > 1 ? prev->load_rd : 0xff;
    }

    /* Instruction fetch */
    {
	int set;
	if (cache_find(&icache, in->pc, &set) < 0)
	{
	    stat_imiss++;
	{
	    uint64_t w = line_fill(in);
	    if (listing)
		in->istall += w;
	}
	    cache_fill(&icache, in->pc, set);
	}
    }

    spend(in, in->base);
    if (listing)
	in->count[cur_region]++;
    region_insns[cur_region]++;
    stat_insns++;
    prev = in;
    prev_loaded = 0;
}

static void vcpu_insn(unsigned int vcpu, void *udata)
{
    run_insn(udata);
}

/* The first instruction of a region's function: its return address is in lr */
static void vcpu_region_insn(unsigned int vcpu, void *udata)
{
    struct insn *in = udata;
    GByteArray *buf = g_byte_array_new();
    uint32_t lr = 0;

    if (reg_lr && qemu_plugin_read_register(reg_lr, buf) == 4)
	memcpy(&lr, buf->data, 4);
    g_byte_array_free(buf, TRUE);
    if (depth < STACK_DEPTH)
    {
	stack[depth].ret = lr & ~1u;
	stack[depth].region = in->region - 1;
	depth++;
	cur_region = in->region - 1;
    }
    run_insn(in);
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    GArray *regs = qemu_plugin_get_registers();
    for (guint i = 0; i < regs->len; i++)
    {
	qemu_plugin_reg_descriptor *r = &g_array_index(regs, qemu_plugin_reg_descriptor, i);
	if (!strcmp(r->name, "lr") || !strcmp(r->name, "r14"))
	    reg_lr = r->handle;
    }
    g_array_free(regs, TRUE);
    if (!reg_lr && g_hash_table_size(region_entry))
	fprintf(stderr, "arm926: can't read lr; regions won't be split\n");
}

static int popcount16(uint32_t x)
{
    return __builtin_popcount(x & 0xffff);
}

/* Decodes an ARM (32-bit) instruction for the timing */
static void decode_arm(struct insn *in, uint32_t w)
{
    uint32_t cond = w >> 28;
    uint32_t rn = (w >> 16) & 15, rd = (w >> 12) & 15, rs = (w >> 8) & 15, rm = w & 15;
    in->base = 1;
    in->taken = 0;
    in->load_rd = 0xff;
    in->load_lat = 1;
    in->reads = 0;

    if (cond == 0xf)
    {
	if (((w >> 25) & 7) == 5)		/* BLX immediate */
	    in->taken = 2;
	return;				/* PLD and the like */
    }
    switch ((w >> 25) & 7)
    {
    case 0:
	if ((w & 0x0ffffff0) == 0x012fff10 || (w & 0x0ffffff0) == 0x012fff30)
	{
	    /* BX, BLX register */
	    in->reads = 1u << rm;
	    in->taken = 2;
	    return;
	}
	if ((w & 0x0fc000f0) == 0x00000090)
	{
	    /* MUL, MLA */
	    in->reads = (1u << rm) | (1u << rs) | ((w & (1 << 21)) ? 1u << rd : 0);
	    in->base = (w & (1 << 21)) ? 3 : 2;
	    return;
	}
	if ((w & 0x0f8000f0) == 0x00800090)
	{
	    /* UMULL, UMLAL, SMULL, SMLAL */
	    in->reads = (1u << rm) | (1u << rs);
	    in->base = (w & (1 << 21)) ? 4 : 3;
	    return;
	}
	if ((w & 0x0fb00ff0) == 0x01000090)
	{
	    /* SWP, SWPB */
	    in->reads = (1u << rn) | (1u << rm);
	    in->base = 2;
	    in->load_rd = rd;
	    return;
	}
	if ((w & 0x0e000090) == 0x00000090 && (w & 0x60))
	{
	    /* LDRH/STRH/LDRSB/LDRSH/LDRD/STRD */
	    int load = (w >> 20) & 1;
	    int sh = (w >> 5) & 3;
	    in->reads = 1u << rn;
	    if (!(w & (1 << 22)))
		in->reads |= 1u << rm;
	    if (!load && sh == 1)
		in->reads |= 1u << rd;			/* STRH */
	    if (!load && sh == 3)
		in->reads |= 3u << rd, in->base = 2;	/* STRD */
	    if (!load && sh == 2)
	    {
		in->base = 2;				/* LDRD */
		in->load_rd = rd + 1;
		return;
	    }
	    if (load)
	    {
		in->load_rd = rd;
		in->load_lat = 2;
		if (rd == 15)
		    in->taken = 4;
	    }
	    return;
	}
	/* Data processing, register operand (or MRS/MSR/CLZ, which cost about the same) */
	{
	    uint32_t op = (w >> 21) & 15;
	    in->reads = 1u << rm;
	    if (op != 13 && op != 15)
		in->reads |= 1u << rn;
	    if ((w & 0x10) && !(w & 0x80))
	    {
		in->reads |= 1u << rs;	/* shift by a register */
		in->base = 2;
	    }
	    if (rd == 15 && !(op >= 8 && op <= 11))
		in->taken = 2;
	}
	return;
    case 1:
	{
	    uint32_t op = (w >> 21) & 15;
	    if (op != 13 && op != 15)
		in->reads |= 1u << rn;
	    if (rd == 15 && !(op >= 8 && op <= 11))
		in->taken = 2;
	}
	return;
    case 2:
    case 3:
	if (((w >> 25) & 7) == 3 && (w & 0x10))
	    return;			/* media/undefined */
	{
	    int load = (w >> 20) & 1;
	    in->reads = 1u << rn;
	    if ((w >> 25) & 1)
		in->reads |= 1u << rm;
	    if (!load)
		in->reads |= 1u << rd;
	    else
	    {
		in->load_rd = rd;
		in->load_lat = (w & (1 << 22)) ? 2 : 1;	/* LDRB */
		if (rd == 15)
		{
		    in->taken = 4;
		    in->load_rd = 0xff;
		}
	    }
	}
	return;
    case 4:
	{
	    int load = (w >> 20) & 1;
	    uint32_t list = w & 0xffff;
	    int n = popcount16(list);
	    in->reads = 1u << rn;
	    in->base = n > 1 ? n : 2;
	    if (!load)
		in->reads |= list;
	    else
	    {
		if (list & 0x8000)
		    in->taken = 4;
		else if (list)
		    in->load_rd = 31 - __builtin_clz(list);
	    }
	}
	return;
    case 5:
	in->taken = 2;			/* B, BL */
	return;
    default:
	return;				/* coprocessor, SWI */
    }
}

/* The same instruction at the same address shares its record; code the
 * dynarec writes over gets a new one. */
static guint pc_word_hash(gconstpointer p)
{
    const struct insn *in = p;
    return (guint) (in->pc * 2654435761u) ^ in->word;
}

static gboolean pc_word_equal(gconstpointer a, gconstpointer b)
{
    const struct insn *x = a, *y = b;
    return x->pc == y->pc && x->word == y->word;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++)
    {
	struct qemu_plugin_insn *qi = qemu_plugin_tb_get_insn(tb, i);
	struct insn key, *in;
	uint32_t w = 0;
	size_t size = qemu_plugin_insn_data(qi, &w, sizeof w);

	key.pc = qemu_plugin_insn_vaddr(qi);
	key.word = size == 4 ? w : (w & 0xffff);
	in = g_hash_table_lookup(by_pc_word, &key);
	if (!in)
	{
	    in = g_new0(struct insn, 1);
	    in->pc = key.pc;
	    in->word = key.word;
	    if (qemu_plugin_insn_size(qi) == 4 && size == 4)
		decode_arm(in, w);
	    else
	    {
		in->thumb = 1;
		in->base = 1;
		in->load_rd = 0xff;
		in->taken = 2;
	    }
	    in->region = GPOINTER_TO_INT(g_hash_table_lookup(region_entry, GINT_TO_POINTER((gint) in->pc)));
	    g_hash_table_insert(by_pc_word, in, in);
	    g_ptr_array_add(all_insns, in);
	}
	if (in->region && reg_lr)
	    qemu_plugin_register_vcpu_insn_exec_cb(qi, vcpu_region_insn, QEMU_PLUGIN_CB_R_REGS, in);
	else
	    qemu_plugin_register_vcpu_insn_exec_cb(qi, vcpu_insn, QEMU_PLUGIN_CB_NO_REGS, in);
	qemu_plugin_register_vcpu_mem_cb(qi, vcpu_mem, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, in);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    FILE *f = fopen(out_path, "w");
    guint marks;
    if (!f)
	return;
    fprintf(f, "model fill %" PRIu64 " write %" PRIu64 " word %" PRIu64 " writeback %" PRIu64 "\n",
	    p_fill, p_write, p_word, p_writeback);
    fprintf(f, "cycles %" PRIu64 " instructions %" PRIu64 " imiss %" PRIu64 " dmiss %" PRIu64
	    " storemiss %" PRIu64 " wbstall %" PRIu64 " fillwait %" PRIu64 "\n",
	    now - stat_cycles_start, stat_insns, stat_imiss, stat_dmiss, stat_smiss, stat_wbstall,
	    stat_fill_wait);
    fprintf(f, "regions");
    for (int r = 0; r < n_regions; r++)
	fprintf(f, " %s", region_names[r]);
    fprintf(f, "\n");
    /* Each frame: the cycles, then the instructions, of each region */
    marks = frame_marks->len / (2 * MAX_REGIONS);
    for (guint i = 1; i < marks; i++)
    {
	const uint64_t *a = &g_array_index(frame_marks, uint64_t, (i - 1) * 2 * MAX_REGIONS);
	const uint64_t *b = &g_array_index(frame_marks, uint64_t, i * 2 * MAX_REGIONS);
	fprintf(f, "frame");
	for (int r = 0; r < n_regions; r++)
	    fprintf(f, " %" PRIu64, b[r] - a[r]);
	for (int r = 0; r < n_regions; r++)
	    fprintf(f, " %" PRIu64, b[MAX_REGIONS + r] - a[MAX_REGIONS + r]);
	fprintf(f, "\n");
    }
    for (guint i = 0; i < all_insns->len; i++)
    {
	struct insn *in = g_ptr_array_index(all_insns, i);
	int first = 1;
	for (int r = 0; r < n_regions; r++)
	    if (in->count[r])
	    {
		fprintf(f, "%" PRIx64 " %d %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			in->pc, r, in->count[r], in->cycles[r], first ? in->istall : 0,
			first ? in->dstall : 0, first ? in->wstall : 0);
		first = 0;
	    }
    }
    fclose(f);
}

static void read_regions(const char *path)
{
    char line[256], name[128];
    unsigned long long addr;
    FILE *f = fopen(path, "r");
    if (!f)
    {
	fprintf(stderr, "arm926: can't read %s\n", path);
	return;
    }
    while (fgets(line, sizeof line, f))
    {
	int r;
	if (sscanf(line, "%llx %127s", &addr, name) != 2)
	    continue;
	for (r = 0; r < n_regions && strcmp(region_names[r], name); r++)
	    ;
	if (r == n_regions)
	{
	    if (n_regions == MAX_REGIONS)
		continue;
	    region_names[n_regions++] = g_strdup(name);
	}
	g_hash_table_insert(region_entry, GINT_TO_POINTER((gint) addr), GINT_TO_POINTER(r + 1));
    }
    fclose(f);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
					   int argc, char **argv)
{
    region_entry = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (int i = 0; i < argc; i++)
    {
	char *eq = strchr(argv[i], '=');
	if (!eq)
	    continue;
	*eq = 0;
	const char *v = eq + 1;
	if (!strcmp(argv[i], "out"))
	    out_path = g_strdup(v);
	else if (!strcmp(argv[i], "mark"))
	    mark_addr = strtoull(v, NULL, 16);
	else if (!strcmp(argv[i], "regions"))
	    read_regions(v);
	else if (!strcmp(argv[i], "first"))
	    first_frame = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "last"))
	    last_frame = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "fill"))
	    p_fill = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "write"))
	    p_write = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "word"))
	    p_word = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "writeback"))
	    p_writeback = strtoull(v, NULL, 0);
	else if (!strcmp(argv[i], "repl"))
	    p_repl = !strcmp(v, "random") ? 2 : !strcmp(v, "counter") ? 1 : 0;
	else if (!strcmp(argv[i], "icache"))
	    icache_bytes = strtoul(v, NULL, 0);
	else if (!strcmp(argv[i], "dcache"))
	    dcache_bytes = strtoul(v, NULL, 0);
	else
	{
	    fprintf(stderr, "arm926: unknown argument %s\n", argv[i]);
	    return -1;
	}
    }
    if (!mark_addr)
	counting = 1;
    cache_init(&icache, icache_bytes, 4);
    cache_init(&dcache, dcache_bytes, 4);
    frame_marks = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    all_insns = g_ptr_array_new();
    by_pc_word = g_hash_table_new(pc_word_hash, pc_word_equal);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
