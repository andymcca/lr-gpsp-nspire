/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licens e as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _WIN32_WCE

#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>

#endif

#if !defined(GP2X_BUILD) && !defined(NSPIRE_BUILD)
#include "gp2x/cpuctrl.h"
#endif

#if defined(NSPIRE_LIBRETRO)
/* Root common.h pulls memory.h and collides with libretro gba_memory.h (MEMORY_H). */
#include "libretro-gpsp/common.h"
#else
#include "common.h"
#endif
#include "gui.h"
#include "font.h"

#if defined(NSPIRE_LIBRETRO)
#include "gba_memory.h"
#include "serial.h"
#include "nspire_frameskip.h"
#include "savestate.h"
#include "nspire_frontend.h"
#include "nspire_keys.h"
#include "nspire_states.h"
#include "nspire_clock.h"
#include "nspire_brightness.h"
#include "nspire_game_list.h"
#endif

#if defined(NSPIRE_LIBRETRO)
/* Top byte of word [26] global / [15] per-game: new frameskip encoding (auto/manual/off). */
#define GPSP_CFG_FS_V2_MARK 0xFF000000u
#define GPSP_GAME_FS_V2_MARK 0xFF000000u

static void nspire_clamp_rom_buffer_size(void)
{
  if (nspire_rom_buffer_size_choice < 2)
    nspire_rom_buffer_size_choice = 2;
  else if (nspire_rom_buffer_size_choice > 32)
    nspire_rom_buffer_size_choice = 32;
}
#endif

#define MAX_PATH 1024

// Blatantly stolen and trimmed from MZX (megazeux.sourceforge.net)

#if defined(GP2X_BUILD) || defined(NSPIRE_BUILD)

#define FILE_LIST_ROWS ((int)((SDL_SCREEN_HEIGHT - 40) / FONT_HEIGHT))
#define FILE_LIST_POSITION 5
#define DIR_LIST_POSITION 260

#else

#define FILE_LIST_ROWS 25
#define FILE_LIST_POSITION 5
#define DIR_LIST_POSITION 360

#endif

#ifdef PSP_BUILD

#define color16(red, green, blue)                                             \
  (blue << 11) | (green << 5) | red                                           \

#elif defined(NSPIRE_BUILD)
#define color16(red, green, blue)                                             \
  ((green & 1) << 15) | (blue << 10) | ((green >> 1) << 5) | red 
  
#else

#define color16(red, green, blue)                                             \
  (red << 11) | (green << 5) | blue                                           \

#endif

#ifdef GP2X_BUILD

#define COLOR_BG            color16(0, 0, 0)

#else

#define COLOR_BG            color16(2, 8, 10)

#endif

#define COLOR_ROM_INFO      color16(22, 36, 26)
#define COLOR_ACTIVE_ITEM   color16(31, 63, 31)
#define COLOR_INACTIVE_ITEM color16(13, 40, 18)
#define COLOR_FRAMESKIP_BAR color16(15, 31, 31)
#define COLOR_HELP_TEXT     color16(16, 40, 24)
#define COLOR_MOVING        color16(31, 50, 4)

#ifdef PSP_BUILD
  u8 *clock_speed_options[] =
  {
    "33MHz", "66MHz", "100MHz", "133MHz", "166MHz", "200MHz", "233MHz",
    "266MHz", "300MHz", "333MHz"
  };
  #define menu_get_clock_speed() \
    clock_speed = (clock_speed_number + 1) * 33
  #define get_clock_speed_number() \
    clock_speed_number = (clock_speed / 33) - 1
#elif defined(WIZ_BUILD)
  u8 *clock_speed_options[] =
  {
    "300MHz", "333MHz", "366MHz", "400MHz", "433MHz",
    "466MHz", "500MHz", "533MHz", "566MHz", "600MHz",
    "633MHz", "666MHz", "700MHz", "733MHz", "766MHz",
    "800MHz", "833MHz", "866MHz", "900MHz"
  };
  #define menu_get_clock_speed() \
    clock_speed = 300 + (clock_speed_number * 3333) / 100
  #define get_clock_speed_number() \
    clock_speed_number = (clock_speed - 300) / 33
#elif defined(GP2X_BUILD)
  u8 *clock_speed_options[] =
  {
    "150MHz", "160MHz", "170MHz", "180MHz", "190MHz",
    "200MHz", "210MHz", "220MHz", "230MHz", "240MHz",
    "250MHz", "260MHz", "270MHz", "280MHz", "290MHz"
  };
  #define menu_get_clock_speed() \
    clock_speed = 150 + clock_speed_number * 10
  #define get_clock_speed_number() \
    clock_speed_number = (clock_speed - 150) / 10
#elif defined(NSPIRE_BUILD)
  u8 *clock_speed_options[] =
  {
    "120MHz", "126MHz", "132MHz", "138MHz", "144MHz",
	"150MHz", "156MHz", "162MHz", "168MHz", "174MHz",
	"180MHz", "186MHz", "192MHz", "198MHz", "204MHz",
	"210MHz", "216MHz", "222MHz", "228MHz", "234MHz",
	"240MHz", "246MHz", "252MHz", "258MHz", "264MHz",
	"270MHz", "276MHz", "282MHz", "288MHz", "294MHz",
	"300MHz"
  };
  #define menu_get_clock_speed() \
    clock_speed = 120 + clock_speed_number * 6
  #define get_clock_speed_number() \
    clock_speed_number = (clock_speed - 120) / 6
#else
  u8 *clock_speed_options[] =
  {
    "0"
  };
  #define menu_get_clock_speed() 0
  #define get_clock_speed_number() 0
#endif


int sort_function(const void *dest_str_ptr, const void *src_str_ptr)
{
  char *dest_str = *((char **)dest_str_ptr);
  char *src_str = *((char **)src_str_ptr);

  if(src_str[0] == '.')
    return 1;

  if(dest_str[0] == '.')
    return -1;

  return strcasecmp(dest_str, src_str);
}

s32 load_file(u8 **wildcards, u8 *result)
{
  DIR *current_dir;
  struct dirent *current_file;
  struct stat file_info;
  u8 current_dir_name[MAX_PATH];
  u8 current_dir_short[81];
  u32 current_dir_length;
  u32 total_filenames_allocated;
  u32 total_dirnames_allocated;
  u8 **file_list;
  u8 **dir_list;
  u32 num_files;
  u32 num_dirs;
  u8 *file_name;
  u32 file_name_length;
  u32 ext_pos = -1;
  u32 chosen_file, chosen_dir;
  u32 dialog_result = 1;
  s32 return_value = 1;
  s32 current_file_selection;
  s32 current_file_scroll_value;
  u32 current_dir_selection;
  u32 current_dir_scroll_value;
  s32 current_file_in_scroll;
  u32 current_dir_in_scroll;
  u32 current_file_number, current_dir_number;
  u32 current_column = 0;
  u32 repeat;
  u32 i;
  gui_action_type gui_action;
#if defined(NSPIRE_LIBRETRO)
  s32 moving = -1;               /* the game being moved (tab), or -1 */
  s32 moving_from = 0;
  u8 **order_before = NULL;      /* the list before the move, for esc */
  char list_status[60] = "";     /* what the last key did, until the next */

  /* The number keys start games here instead of moving. */
  nspire_gui_digits_move = 0;
#endif

  while(return_value == 1)
  {
    current_file_selection = 0;
    current_file_scroll_value = 0;
    current_dir_selection = 0;
    current_dir_scroll_value = 0;
    current_file_in_scroll = 0;
    current_dir_in_scroll = 0;

    total_filenames_allocated = 32;
    total_dirnames_allocated = 32;
    file_list = (u8 **)malloc(sizeof(u8 *) * 32);
    dir_list = (u8 **)malloc(sizeof(u8 *) * 32);
    memset(file_list, 0, sizeof(u8 *) * 32);
    memset(dir_list, 0, sizeof(u8 *) * 32);

    num_files = 0;
    num_dirs = 0;
    chosen_file = 0;
    chosen_dir = 0;

    getcwd(current_dir_name, MAX_PATH);

    current_dir = opendir(current_dir_name);

    do
    {
	  if(current_dir)
        current_file = readdir(current_dir);
      else
        current_file = NULL;

      if(current_file)
      {
        file_name = current_file->d_name;
        file_name_length = strlen(file_name);

        if((stat(file_name, &file_info) >= 0) &&
         ((file_name[0] != '.') || (file_name[1] == '.')))
        {
          if(S_ISDIR(file_info.st_mode))
          {
            dir_list[num_dirs] =
             (u8 *)malloc(file_name_length + 1);

            sprintf(dir_list[num_dirs], "%s", file_name);

            num_dirs++;
          }
          else
          {
			// Must match one of the wildcards, also ignore the .
#ifdef NSPIRE_BUILD
			if(file_name_length >= 8)
			{
			  if(file_name[file_name_length - 8] == '.')
			    ext_pos = file_name_length - 8;
#else
            if(file_name_length >= 4)
            {
              if(file_name[file_name_length - 4] == '.')
                ext_pos = file_name_length - 4;
              else

              if(file_name[file_name_length - 3] == '.')
                ext_pos = file_name_length - 3;
#endif
              else
                ext_pos = 0;

              for(i = 0; wildcards[i] != NULL; i++)
              {
                if(!strcasecmp((file_name + ext_pos),
                 wildcards[i]))
                {
                  file_list[num_files] =
                   (u8 *)malloc(file_name_length + 1);

                  sprintf(file_list[num_files], "%s", file_name);

                  num_files++;
                  break;
                }
              }
            }
          }
        }

        if(num_files == total_filenames_allocated)
        {
          file_list = (u8 **)realloc(file_list, sizeof(u8 *) *
           total_filenames_allocated * 2);
          memset(file_list + total_filenames_allocated, 0,
           sizeof(u8 *) * total_filenames_allocated);
          total_filenames_allocated *= 2;
        }

        if(num_dirs == total_dirnames_allocated)
        {
          dir_list = (u8 **)realloc(dir_list, sizeof(u8 *) *
           total_dirnames_allocated * 2);
          memset(dir_list + total_dirnames_allocated, 0,
           sizeof(u8 *) * total_dirnames_allocated);
          total_dirnames_allocated *= 2;
        }
      }
    } while(current_file);
#ifndef NSPIRE_BUILD
    qsort((void *)file_list, num_files, sizeof(u8 *), sort_function);
    qsort((void *)dir_list, num_dirs, sizeof(u8 *), sort_function);
#endif
    closedir(current_dir);
#if defined(NSPIRE_LIBRETRO)
    game_list_sort((char **)file_list, num_files);
    game_list_sort((char **)dir_list, num_dirs);
    game_list_apply_order((char **)file_list, num_files, (char *)current_dir_name);
    moving = -1;
    order_before = (u8 **)malloc(sizeof(u8 *) * (num_files ? num_files : 1));
#endif

    current_dir_length = strlen(current_dir_name);

    if(current_dir_length > 80)
    {

#if defined(GP2X_BUILD)
    snprintf(current_dir_short, 80,
     "...%s", current_dir_name + current_dir_length - 77);
#else
    memcpy(current_dir_short, "...", 3);
      memcpy(current_dir_short + 3,
       current_dir_name + current_dir_length - 77, 77);
      current_dir_short[80] = 0;
#endif
    }
    else
    {
#if defined(GP2X_BUILD)
      snprintf(current_dir_short, 80, "%s", current_dir_name);
#else
      memcpy(current_dir_short, current_dir_name,
       current_dir_length + 1);
#endif
    }

    repeat = 1;

#if defined(NSPIRE_LIBRETRO)
    /* A folder just opened starts on its first game, if it has one. */
    current_column = num_files == 0;
#else
    if(num_files == 0)
      current_column = 1;
#endif

    clear_screen(COLOR_BG);
  {
    u8 print_buffer[81];

    while(repeat)
    {
      flip_screen();
#ifdef NSPIRE_BUILD
	  update_at_vblank();
#endif
	  
#ifdef NSPIRE_BUILD
	clear_screen(COLOR_BG);
#endif

      print_string(current_dir_short, COLOR_ACTIVE_ITEM, COLOR_BG, 0, 0);
#ifdef GP2X_BUILD
      print_string("Press X to return to the main menu.",
       COLOR_HELP_TEXT, COLOR_BG, 20, 220);
#elif defined(NSPIRE_LIBRETRO)
      if(moving >= 0)
      {
        print_string("Moving it: up/down, then enter to put it there,",
         COLOR_HELP_TEXT, COLOR_BG, 20, 220);
        print_string("or esc to put it back.", COLOR_HELP_TEXT, COLOR_BG, 20, 230);
      }
      else
      {
        print_string(list_status[0] ? list_status :
         "enter or 1-9, 0: start  shift: without its state",
         COLOR_HELP_TEXT, COLOR_BG, 20, 220);
        print_string("tab: move a game  shift+tab: A to Z  menu: back",
         COLOR_HELP_TEXT, COLOR_BG, 20, 230);
      }
#elif defined(NSPIRE_BUILD)
	  print_string("Press menu to return to the main menu.",
       COLOR_HELP_TEXT, COLOR_BG, 20, 220);
#else
      print_string("Press X to return to the main menu.",
       COLOR_HELP_TEXT, COLOR_BG, 20, 260);
#endif

      for(i = 0, current_file_number = i + current_file_scroll_value;
       i < FILE_LIST_ROWS; i++, current_file_number++)
      {
        if(current_file_number < num_files)
        {
#if defined(NSPIRE_LIBRETRO)
          /* The first ten start with their number key, shown before them. */
          char row[100];
          u16 color = COLOR_INACTIVE_ITEM;

          if((s32)current_file_number == moving)
            color = COLOR_MOVING;
          else if((current_file_number == current_file_selection) &&
           (current_column == 0))
            color = COLOR_ACTIVE_ITEM;
          if(current_file_number < 10)
            snprintf(row, sizeof(row), "%u %s", (current_file_number + 1) % 10,
             (char *)file_list[current_file_number]);
          else
            snprintf(row, sizeof(row), "  %s", (char *)file_list[current_file_number]);
          print_string(row, color, COLOR_BG, FILE_LIST_POSITION, ((i + 1) * 10));
#else
          if((current_file_number == current_file_selection) &&
           (current_column == 0))
          {
            print_string(file_list[current_file_number], COLOR_ACTIVE_ITEM,
             COLOR_BG, FILE_LIST_POSITION, ((i + 1) * 10));
          }
          else
          {
            print_string(file_list[current_file_number], COLOR_INACTIVE_ITEM,
             COLOR_BG, FILE_LIST_POSITION, ((i + 1) * 10));
          }
#endif
        }
      }

      for(i = 0, current_dir_number = i + current_dir_scroll_value;
       i < FILE_LIST_ROWS; i++, current_dir_number++)
      {
        if(current_dir_number < num_dirs)
        {
          if((current_dir_number == current_dir_selection) &&
           (current_column == 1))
          {
            print_string(dir_list[current_dir_number], COLOR_ACTIVE_ITEM,
             COLOR_BG, DIR_LIST_POSITION, ((i + 1) * 10));
          }
          else
          {
            print_string(dir_list[current_dir_number], COLOR_INACTIVE_ITEM,
             COLOR_BG, DIR_LIST_POSITION, ((i + 1) * 10));
          }
        }
      }

      gui_action = get_gui_input();

#if defined(NSPIRE_LIBRETRO)
      {
        s32 digit = nspire_digit_pressed();
        int tab = key_pressed(KEY_TAB);

        if(gui_action != CURSOR_NONE || digit >= 0 || tab)
          list_status[0] = 0;

        if(moving >= 0)
        {
          /* Up/down carry the game (the selection follows it below), enter
           * or tab puts it there, esc (or menu) puts it back. */
          if(tab)
            gui_action = CURSOR_SELECT;
          switch(gui_action)
          {
            case CURSOR_UP:
              if(current_file_selection > 0)
                game_list_move((char **)file_list, num_files, current_file_selection,
                 current_file_selection - 1);
              else
                gui_action = CURSOR_NONE;
              break;

            case CURSOR_DOWN:
              if(current_file_selection < (s32)num_files - 1)
                game_list_move((char **)file_list, num_files, current_file_selection,
                 current_file_selection + 1);
              else
                gui_action = CURSOR_NONE;
              break;

            case CURSOR_SELECT:
              if(!game_list_save_order((char **)file_list, num_files,
               (char *)current_dir_name))
                snprintf(list_status, sizeof(list_status), "Couldn't save the order.");
              moving = -1;
              gui_action = CURSOR_NONE;
              break;

            case CURSOR_BACK:
            case CURSOR_EXIT:
              memcpy(file_list, order_before, sizeof(u8 *) * num_files);
              current_file_selection = moving_from;
              /* Keep it on the screen. */
              if(current_file_selection < current_file_scroll_value)
                current_file_scroll_value = current_file_selection;
              else if(current_file_selection >= current_file_scroll_value + FILE_LIST_ROWS)
                current_file_scroll_value = current_file_selection - FILE_LIST_ROWS + 1;
              current_file_in_scroll = current_file_selection - current_file_scroll_value;
              moving = -1;
              if(gui_action == CURSOR_BACK)
                gui_action = CURSOR_NONE;
              break;

            default:
              gui_action = CURSOR_NONE;
              break;
          }
        }
        else if(digit >= 0 && digit < (s32)num_files)
        {
          current_file_selection = digit;
          current_column = 0;
          gui_action = CURSOR_SELECT;
        }
        else if(tab && current_column == 0 && num_files > 1 && order_before)
        {
          if(key_down(KEY_SHIFT))
          {
            game_list_forget_order((char *)current_dir_name);
            game_list_sort((char **)file_list, num_files);
            snprintf(list_status, sizeof(list_status), "Sorted A to Z.");
          }
          else
          {
            memcpy(order_before, file_list, sizeof(u8 *) * num_files);
            moving = moving_from = current_file_selection;
          }
        }
      }
#endif

      switch(gui_action)
      {
        case CURSOR_DOWN:
          if(current_column == 0)
          {
            if(current_file_selection < (num_files - 1))
            {
              current_file_selection++;
              if(current_file_in_scroll == (FILE_LIST_ROWS - 1))
              {
                clear_screen(COLOR_BG);
                current_file_scroll_value++;
              }
              else
              {
                current_file_in_scroll++;
              }
            }
            else
            {
              clear_screen(COLOR_BG);
              current_file_selection = 0;
              current_file_scroll_value = 0;
              current_file_in_scroll = 0;
            }
          }
          else
          {
            if(current_dir_selection < (num_dirs - 1))
            {
              current_dir_selection++;
              if(current_dir_in_scroll == (FILE_LIST_ROWS - 1))
              {
                clear_screen(COLOR_BG);
                current_dir_scroll_value++;
              }
              else
              {
                current_dir_in_scroll++;
              }
            }
          }

          break;

        case CURSOR_R:
          if (current_column != 0)
            break;
          clear_screen(COLOR_BG);
	  current_file_selection += FILE_LIST_ROWS;
          if (current_file_selection > num_files - 1)
            current_file_selection = num_files - 1;
          current_file_scroll_value = current_file_selection - FILE_LIST_ROWS / 2;
          if (current_file_scroll_value < 0)
          {
            current_file_scroll_value = 0;
            current_file_in_scroll = current_file_selection;
          }
          else
          {
            current_file_in_scroll = FILE_LIST_ROWS / 2;
          }
          break;

        case CURSOR_UP:
          if(current_column == 0)
          {
            if(current_file_selection)
            {
              current_file_selection--;
              if(current_file_in_scroll == 0)
              {
                clear_screen(COLOR_BG);
                current_file_scroll_value--;
              }
              else
              {
                current_file_in_scroll--;
              }
            }
            else
            {
              clear_screen(COLOR_BG);
              current_file_selection = num_files - 1;
              current_file_in_scroll = FILE_LIST_ROWS - 1;
              if (current_file_in_scroll > num_files - 1)
                current_file_in_scroll = num_files - 1;
              current_file_scroll_value = num_files - FILE_LIST_ROWS;
              if (current_file_scroll_value < 0)
                current_file_scroll_value = 0;
            }
          }
          else
          {
            if(current_dir_selection)
            {
              current_dir_selection--;
              if(current_dir_in_scroll == 0)
              {
                clear_screen(COLOR_BG);
                current_dir_scroll_value--;
              }
              else
              {
                current_dir_in_scroll--;
              }
            }
          }
          break;

        case CURSOR_L:
          if (current_column != 0)
            break;
          clear_screen(COLOR_BG);
	  current_file_selection -= FILE_LIST_ROWS;
          if (current_file_selection < 0)
            current_file_selection = 0;
          current_file_scroll_value = current_file_selection - FILE_LIST_ROWS / 2;
          if (current_file_scroll_value < 0)
          {
            current_file_scroll_value = 0;
            current_file_in_scroll = current_file_selection;
          }
          else
          {
            current_file_in_scroll = FILE_LIST_ROWS / 2;
          }
          break;

         case CURSOR_RIGHT:
          if(current_column == 0)
          {
            if(num_dirs != 0)
              current_column = 1;
          }
          break;

        case CURSOR_LEFT:
          if(current_column == 1)
          {
            if(num_files != 0)
              current_column = 0;
          }
          break;

        case CURSOR_SELECT:
          if(current_column == 1)
          {
            repeat = 0;
            chdir(dir_list[current_dir_selection]);
          }
          else
          {
            if(num_files != 0)
            {
              repeat = 0;
              return_value = 0;
              strcpy(result, file_list[current_file_selection]);
#if defined(NSPIRE_LIBRETRO)
              nspire_skip_state_load = key_down(KEY_SHIFT);
#endif
            }
          }
          break;

        case CURSOR_BACK:
#ifdef PSP_BUILD
          if(!strcmp(current_dir_name, "ms0:/PSP"))
            break;
#endif
#ifdef NSPIRE_BUILD
		  //if(!strcmp(current_dir_name, "\\documents\\"))
			//break;
#endif
          repeat = 0;
          chdir("..");
          break;

        case CURSOR_EXIT:
          return_value = -1;
          repeat = 0;
          break;
      }
#if defined(NSPIRE_LIBRETRO)
      if(moving >= 0)
        moving = current_file_selection;
#endif
    }
  }

    for(i = 0; i < num_files; i++)
    {
      free(file_list[i]);
    }
    free(file_list);
#if defined(NSPIRE_LIBRETRO)
    free(order_before);
    order_before = NULL;
#endif

    for(i = 0; i < num_dirs; i++)
    {
      free(dir_list[i]);
    }
    free(dir_list);
  }

  clear_screen(COLOR_BG);
#if defined(NSPIRE_LIBRETRO)
  nspire_gui_digits_move = 1;
#endif

  return return_value;
}

typedef enum
{
  NUMBER_SELECTION_OPTION = 0x01,
  STRING_SELECTION_OPTION = 0x02,
  SUBMENU_OPTION          = 0x04,
  ACTION_OPTION           = 0x08,
  KEY_SELECTION_OPTION    = 0x10,
  KEY_BINDING_OPTION      = 0x20,  /* Nspire: an action's two key bindings */
  SWIPE_MULTI_OPTION      = 0x40,  /* Nspire: GBA buttons one swipe holds */
  COMBO_SLOT_OPTION       = 0x80   /* Nspire: one key or swipe, several buttons */
} menu_option_type_enum;

struct _menu_type
{
  void (* init_function)();
  void (* passive_function)();
  struct _menu_option_type *options;
  u32 num_options;
};

struct _menu_option_type
{
  void (* action_function)();
  void (* passive_function)();
  struct _menu_type *sub_menu;
  char *display_string;
  void *options;
  u32 *current_option;
  u32 num_options;
  char *help_string;
  u32 line_number;
  menu_option_type_enum option_type;
};

typedef struct _menu_option_type menu_option_type;
typedef struct _menu_type menu_type;

#define make_menu(name, init_function, passive_function)                      \
  menu_type name##_menu =                                                     \
  {                                                                           \
    init_function,                                                            \
    passive_function,                                                         \
    name##_options,                                                           \
    sizeof(name##_options) / sizeof(menu_option_type)                         \
  }                                                                           \

#define gamepad_config_option(display_string, number)                         \
{                                                                             \
  NULL,                                                                       \
  menu_fix_gamepad_help,                                                      \
  NULL,                                                                       \
  display_string ": %s",                                                      \
  gamepad_config_buttons,                                                     \
  gamepad_config_map + gamepad_config_line_to_button[number],                 \
  sizeof(gamepad_config_buttons) / sizeof(gamepad_config_buttons[0]),         \
  gamepad_help[gamepad_config_map[                                            \
   gamepad_config_line_to_button[number]]],                                   \
  number,                                                                     \
  STRING_SELECTION_OPTION                                                     \
}                                                                             \

#define analog_config_option(display_string, number)                          \
{                                                                             \
  NULL,                                                                       \
  menu_fix_gamepad_help,                                                      \
  NULL,                                                                       \
  display_string ": %s",                                                      \
  gamepad_config_buttons,                                                     \
  gamepad_config_map + number + 12,                                           \
  sizeof(gamepad_config_buttons) / sizeof(gamepad_config_buttons[0]),         \
  gamepad_help[gamepad_config_map[number + 12]],                              \
  number + 2,                                                                 \
  STRING_SELECTION_OPTION                                                     \
}                                                                             \

#ifdef NSPIRE_BUILD
/* A line of "Configure input": an action (in num_options) and its two key
 * bindings (nspire_keys.h). */
#define binding_option(action, line_number)                                   \
{                                                                             \
  NULL, NULL, NULL, NULL, NULL, NULL, action,                                 \
  "Left/right: key 1 or 2. Enter: set it (a key\n"                       \
  "or a swipe, or hold one and press another).\n"                            \
  "Del clears it.",                                                           \
  line_number, KEY_BINDING_OPTION                                             \
}

#define swipe_multi_option(dir_index, line_number)                            \
{                                                                             \
  NULL, NULL, NULL, NULL, NULL, NULL, dir_index,                              \
  "Enter picks the GBA buttons this swipe holds.\n"                           \
  "A list here replaces that swipe's key row.\n"                              \
  "Del clears it.",                                                           \
  line_number, SWIPE_MULTI_OPTION                                             \
}

#define combo_slot_option(slot_index, line_number)                            \
{                                                                             \
  NULL, NULL, NULL, NULL, NULL, NULL, slot_index,                             \
  "Left/right: trigger or buttons. Enter sets\n"                              \
  "the trigger (a key or a swipe) or the buttons.\n"                          \
  "Del clears that side.",                                                    \
  line_number, COMBO_SLOT_OPTION                                              \
}

/* The main menu's slot lines, and the last line of options on the screen
 * (y 200): longer menus scroll. */
#define LOAD_SLOT_LINE 3
#define SAVE_SLOT_LINE 4
#define MENU_LAST_LINE 16
#endif

#if defined(NSPIRE_LIBRETRO)
#define MENU_SLOTS STATE_SLOTS
#else
#define MENU_SLOTS 10
#define LOAD_SLOT_LINE 3
#define SAVE_SLOT_LINE 4
#endif
  
#define cheat_option(number)                                                  \
{                                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  NULL,                                                                       \
  cheat_format_str[number],                                                   \
  enable_disable_options,                                                     \
  &(cheats[number].cheat_active),                                             \
  2,                                                                          \
  "Activate/deactivate this cheat code.",                                     \
  number,                                                                     \
  STRING_SELECTION_OPTION                                                     \
}                                                                             \

#define action_option(action_function, passive_function, display_string,      \
 help_string, line_number)                                                    \
{                                                                             \
  action_function,                                                            \
  passive_function,                                                           \
  NULL,                                                                       \
  display_string,                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  0,                                                                          \
  help_string,                                                                \
  line_number,                                                                \
  ACTION_OPTION                                                               \
}                                                                             \

#define submenu_option(sub_menu, display_string, help_string, line_number)    \
{                                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  sub_menu,                                                                   \
  display_string,                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  sizeof(sub_menu) / sizeof(menu_option_type),                                \
  help_string,                                                                \
  line_number,                                                                \
  SUBMENU_OPTION                                                              \
}                                                                             \

#define selection_option(passive_function, display_string, options,           \
 option_ptr, num_options, help_string, line_number, type)                     \
{                                                                             \
  NULL,                                                                       \
  passive_function,                                                           \
  NULL,                                                                       \
  display_string,                                                             \
  options,                                                                    \
  option_ptr,                                                                 \
  num_options,                                                                \
  help_string,                                                                \
  line_number,                                                                \
  type                                                                        \
}                                                                             \

#define action_selection_option(action_function, passive_function,            \
 display_string, options, option_ptr, num_options, help_string, line_number,  \
 type)                                                                        \
{                                                                             \
  action_function,                                                            \
  passive_function,                                                           \
  NULL,                                                                       \
  display_string,                                                             \
  options,                                                                    \
  option_ptr,                                                                 \
  num_options,                                                                \
  help_string,                                                                \
  line_number,                                                                \
  type | ACTION_OPTION                                                        \
}                                                                             \


#define string_selection_option(passive_function, display_string, options,    \
 option_ptr, num_options, help_string, line_number)                           \
  selection_option(passive_function, display_string ": %s", options,          \
   option_ptr, num_options, help_string, line_number, STRING_SELECTION_OPTION)\

#define numeric_selection_option(passive_function, display_string,            \
 option_ptr, num_options, help_string, line_number)                           \
  selection_option(passive_function, display_string ": %d", NULL, option_ptr, \
   num_options, help_string, line_number, NUMBER_SELECTION_OPTION)            \

#define string_selection_(action_function, passive_function,     \
 display_string, options, option_ptr, num_options, help_string, line_number)  \
  action_selection_option(action_function, passive_function,                  \
   display_string ": %s",  options, option_ptr, num_options, help_string,     \
   line_number, STRING_SELECTION_OPTION)                                      \

#define numeric_selection_action_option(action_function, passive_function,    \
 display_string, option_ptr, num_options, help_string, line_number)           \
  action_selection_option(action_function, passive_function,                  \
   display_string ": %d",  NULL, option_ptr, num_options, help_string,        \
   line_number, NUMBER_SELECTION_OPTION)                                      \

#define numeric_selection_action_hide_option(action_function,                 \
 passive_function, display_string, option_ptr, num_options, help_string,      \
 line_number)                                                                 \
  action_selection_option(action_function, passive_function,                  \
   display_string, NULL, option_ptr, num_options, help_string,                \
   line_number, NUMBER_SELECTION_OPTION)                                      \

#define GAMEPAD_MENU_WIDTH 15

#ifdef PSP_BUILD

u32 gamepad_config_line_to_button[] =
 { 8, 6, 7, 9, 1, 2, 3, 0, 4, 5, 11, 10 };

#endif

#ifdef GP2X_BUILD

u32 gamepad_config_line_to_button[] =
 { 0, 2, 1, 3, 8, 9, 10, 11, 6, 7, 4, 5, 14 };

#endif

#ifdef NSPIRE_BUILD

u32 gamepad_config_line_to_button[] =
 { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

/* Key names, by gpSP's key numbers (nspire_keys.c). */
extern char *nspire_key_strings[];
 
#endif

u8 *scale_options[] =
{
#ifdef WIZ_BUILD
  "unscaled 3:2", "scaled 3:2 (slower)",
  "unscaled 3:2 (anti-tear)", "scaled 3:2 (anti-tear)"
#elif NSPIRE_BUILD
  "unscaled 3:2 (small)", "scaled/blended 3:2 (slowest)", "fullscreen/blended (slower)", "scaled/unblended 3:2 (fast)"
#else
  "unscaled 3:2", "scaled 3:2", "fullscreen", "scaled 3:2 (software)"
#ifdef PSP_BUILD
  " 16:9"
#endif
#endif
};

#if defined(NSPIRE_LIBRETRO)

/* gpSP's settings files: see nspire_config.c. */
s32 load_game_config_file()
{
  return 0;   /* a game's own settings open with it (nspire_start_game) */
}

s32 load_config_file()
{
  return nspire_config_load();
}

s32 save_game_config_file()
{
  return 0;   /* written with save_config_file */
}

s32 save_config_file()
{
  return nspire_config_save();
}

#else
s32 load_game_config_file()
{
  u8 game_config_filename[512];
  u32 file_loaded = 0;
  u32 i;
#ifdef NSPIRE_BUILD
  change_ext(gamepak_filename, game_config_filename, ".cfg.tns");
#else
  change_ext(gamepak_filename, game_config_filename, ".cfg");
#endif

  file_open(game_config_file, game_config_filename, read);

  if(file_check_valid(game_config_file))
  {
    u32 file_size = file_length(game_config_filename, game_config_file);

#if defined(NSPIRE_LIBRETRO)
    if (file_size == 64)
    {
      u32 file_options[16];

      file_read_array(game_config_file, file_options);
      {
        u32 r0 = file_options[0];
        u32 r15 = file_options[15];
        if ((r15 & GPSP_GAME_FS_V2_MARK) == GPSP_GAME_FS_V2_MARK)
          current_frameskip_type = (frameskip_type)(r0 % 3);
        else
        {
          static const frameskip_type om[] = {
            no_frameskip, auto_frameskip, auto_frameskip, manual_frameskip
          };
          current_frameskip_type = om[r0 % 4];
        }
      }
      frameskip_value = file_options[1];
      random_skip = file_options[2] % 2;
      clock_speed = file_options[3];

      if (clock_speed > 300)
        clock_speed = 132;
      if (clock_speed < 33)
        clock_speed = 33;

      if (frameskip_value > 99)
        frameskip_value = 99;
      if (frameskip_value < 1 && current_frameskip_type == auto_frameskip)
        frameskip_value = 4;

      for (i = 0; i < 10; i++)
      {
        cheats[i].cheat_active = file_options[4 + i] % 2;
        cheats[i].cheat_name[0] = 0;
      }

      quick_save_slot = file_options[14];

      file_loaded = 1;
    }
    else if (file_size == 60)
    {
      u32 file_options[15];

      file_read_array(game_config_file, file_options);
      {
        u32 old_type = file_options[0] % 3;
        if (old_type == 2)
          current_frameskip_type = no_frameskip;
        else if (old_type == 1)
          current_frameskip_type = manual_frameskip;
        else
          current_frameskip_type = auto_frameskip;
      }
      frameskip_value = file_options[1];
      if (frameskip_value > 99)
        frameskip_value = 99;
      if (frameskip_value < 1 && current_frameskip_type == auto_frameskip)
        frameskip_value = 4;

      random_skip = file_options[2] % 2;
      clock_speed = file_options[3];

      if (clock_speed > 300)
        clock_speed = 132;
      if (clock_speed < 33)
        clock_speed = 33;

      for (i = 0; i < 10; i++)
      {
        cheats[i].cheat_active = file_options[4 + i] % 2;
        cheats[i].cheat_name[0] = 0;
      }

      quick_save_slot = file_options[14];

      file_loaded = 1;
    }
#else
    /* Sanity check: File size must be the right size */
    if(file_size == 60)
    {
      u32 file_options[file_size / 4];

      file_read_array(game_config_file, file_options);
      current_frameskip_type = file_options[0] % 3;
      frameskip_value = file_options[1];
      random_skip = file_options[2] % 2;
      clock_speed = file_options[3];

#ifdef WIZ_BUILD
      if(clock_speed > 900)
        clock_speed = 533;
#elif defined(GP2X_BUILD)
      if(clock_speed >= 300)
        clock_speed = 200;
#elif defined(PSP_BUILD)
      if(clock_speed > 333)
        clock_speed = 333;
#elif defined(NSPIRE_BUILD)
	  if(clock_speed > 300)
	    clock_speed = 132;
#endif

      if(clock_speed < 33)
        clock_speed = 33;

      if(frameskip_value < 0)
        frameskip_value = 0;

      if(frameskip_value > 99)
        frameskip_value = 99;

      for(i = 0; i < 10; i++)
      {
        cheats[i].cheat_active = file_options[4 + i] % 2;
        cheats[i].cheat_name[0] = 0;
      }
      
	  quick_save_slot = file_options[14];
	  
      file_loaded = 1;
    }
#endif
	file_close(game_config_file);
  }

  if(file_loaded)
    return 0;

#if defined(NSPIRE_LIBRETRO)
  current_frameskip_type = manual_frameskip;
  frameskip_value = 2;
  random_skip = 0;
#else
  current_frameskip_type = auto_frameskip;
  frameskip_value = 4;
#ifdef WIZ_BUILD
  frameskip_value = 1;
#endif
  random_skip = 0;
#endif
  clock_speed = default_clock_speed;

  for(i = 0; i < 10; i++)
  {
    cheats[i].cheat_active = 0;
    cheats[i].cheat_name[0] = 0;
  }

  return -1;
}

s32 load_config_file()
{
  u8 config_path[512];

  #if (defined(PSP_BUILD) || defined(ARM_ARCH)) && !defined(_WIN32_WCE)
    sprintf(config_path, "%s/%s", main_path, GPSP_CONFIG_FILENAME);
  #else
    sprintf(config_path, "%s\\%s", main_path, GPSP_CONFIG_FILENAME);
  #endif

  file_open(config_file, config_path, read);

  if(file_check_valid(config_file))
  {
    u32 file_size = file_length(config_path, config_file);

    // Sanity check: file sizes by feature growth:
    // 96 = +BIOS, 112 = +frameskip, 124 = +emulator options, 128 = +ROM buffer size,
    // 132 = +LCD cache mode, 136 = +FPS overlay, 140 = +GBA blend skip, 144 = +dynarec SMC policy,
    // 148 = legacy (extra u32 for removed RAM block reuse option; ignored), 152 = +video renderer.
    if(file_size == 92
#if defined(NSPIRE_LIBRETRO)
     || file_size == 96
     || file_size == 112
     || file_size == 124
     || file_size == 128
     || file_size == 132
     || file_size == 136
     || file_size == 140
     || file_size == 144
     || file_size == 148
     || file_size == 152
#endif
     )
    {
      u32 words = file_size / 4;
#if defined(NSPIRE_LIBRETRO)
      u32 file_options[38];
#else
      u32 file_options[28];
#endif
      u32 i;
      s32 menu_button = -1;

      memset(file_options, 0, sizeof(file_options));
      file_read(config_file, file_options, words * sizeof(u32));

      screen_scale = file_options[0] %
        (sizeof(scale_options) / sizeof(scale_options[0]));
      screen_filter = file_options[1] % 2;
      global_enable_audio = file_options[2] % 2;

#ifdef PSP_BUILD
      audio_buffer_size_number = file_options[3] % 10;
#else
      audio_buffer_size_number = file_options[3] % 11;
#endif

      update_backup_flag = file_options[4] % 2;
      global_enable_analog = file_options[5] % 2;
      analog_sensitivity_level = file_options[6] % 8;

#ifdef PSP_BUILD
    scePowerSetClockFrequency(clock_speed, clock_speed, clock_speed / 2);
#endif

      // Sanity check: Make sure there's a MENU or FRAMESKIP
      // key, if not assign to triangle

#ifndef PC_BUILD
#ifdef NSPIRE_BUILD
	  for(i = 0; i < 16; i++)
	  {
	    gamepad_config_map[i] = file_options[7 + i];
		if(gamepad_config_map[i] > 128 || nspire_key_strings[gamepad_config_map[i]] == NULL)
			gamepad_config_map[i] = 0;
	  }
	  if(!gamepad_config_map[10]) //KEY_MAP_MENU
		gamepad_config_map[10] = 102; //KEY_MENU
#else
      for(i = 0; i < 16; i++)
      {
        gamepad_config_map[i] = file_options[7 + i] %
         (BUTTON_ID_NONE + 1);

        if(gamepad_config_map[i] == BUTTON_ID_MENU)
        {
          menu_button = i;
        }
      }

      if(menu_button == -1)
      {
        gamepad_config_map[0] = BUTTON_ID_MENU;
      }
#endif
#endif

#if defined(NSPIRE_LIBRETRO)
      if (words >= 24)
        nspire_bios_choice = file_options[23] % 2;
      else
        nspire_bios_choice = 1;

      if (words >= 28)
      {
        u32 r24 = file_options[24];
        u32 r26 = file_options[26];
        if ((r26 & GPSP_CFG_FS_V2_MARK) == GPSP_CFG_FS_V2_MARK)
          current_frameskip_type = (frameskip_type)(r24 % 3);
        else
        {
          static const frameskip_type om[] = {
            no_frameskip, auto_frameskip, auto_frameskip, manual_frameskip
          };
          current_frameskip_type = om[r24 % 4];
        }
        frameskip_value = file_options[25];
        if (frameskip_value > 99)
          frameskip_value = 99;
        if (frameskip_value < 1 && current_frameskip_type == auto_frameskip)
          frameskip_value = 4;
        random_skip = file_options[27] % 2;
      }

      if (words >= 31)
      {
        nspire_sprlim_choice = file_options[28] % 2;
        nspire_rtc_choice = file_options[29] % 3;
        nspire_frame_mix_choice = file_options[30] % 2;
      }
      if (words >= 32)
        nspire_rom_buffer_size_choice = file_options[31];
      else
        nspire_rom_buffer_size_choice = 8;
      if (words >= 33)
        nspire_lcd_cache_clean_full = file_options[32] % 2;
      else
        nspire_lcd_cache_clean_full = 0;
      if (words >= 34)
        nspire_fps_overlay = file_options[33] % 2;
      else
        nspire_fps_overlay = 0;
      if (words >= 35)
        nspire_gba_blend_off = file_options[34] % 2;
      else
        nspire_gba_blend_off = 0;
      if (words >= 36)
        nspire_dynarec_ram_policy = file_options[35] % 2;
      else
        nspire_dynarec_ram_policy = 0;
      if (words >= 37)
        nspire_video_renderer_choice = file_options[36] % 2;
      else
        nspire_video_renderer_choice = 0;
      nspire_clamp_rom_buffer_size();
      nspire_emulator_options_apply();
#endif

      file_close(config_file);
    }

    return 0;
  }

  return -1;
}

s32 save_game_config_file()
{
  u8 game_config_filename[512];
  u32 i;

#ifdef NSPIRE_BUILD
  change_ext(gamepak_filename, game_config_filename, ".cfg.tns");
#else
  change_ext(gamepak_filename, game_config_filename, ".cfg");
#endif

  file_open(game_config_file, game_config_filename, write);

  if(file_check_valid(game_config_file))
  {
#if defined(NSPIRE_LIBRETRO)
    u32 file_options[16];

    file_options[0] = (u32)current_frameskip_type % 3;
    file_options[1] = frameskip_value;
    file_options[2] = random_skip;
    file_options[3] = clock_speed;

    for(i = 0; i < 10; i++)
    {
      file_options[4 + i] = cheats[i].cheat_active;
    }

    file_options[14] = quick_save_slot;
    file_options[15] = GPSP_GAME_FS_V2_MARK;

    file_write(game_config_file, file_options, sizeof(file_options));
#else
    u32 file_options[15];

    file_options[0] = current_frameskip_type;
    file_options[1] = frameskip_value;
    file_options[2] = random_skip;
    file_options[3] = clock_speed;

    for(i = 0; i < 10; i++)
    {
      file_options[4 + i] = cheats[i].cheat_active;
    }
	
	file_options[14] = quick_save_slot;
	
    file_write_array(game_config_file, file_options);
#endif

    file_close(game_config_file);

    return 0;
  }

  return -1;
}

s32 save_config_file()
{
  u8 config_path[512];

  #if (defined(PSP_BUILD) || defined(ARM_ARCH)) && !defined(_WIN32_WCE)
    sprintf(config_path, "%s/%s", main_path, GPSP_CONFIG_FILENAME);
  #else
    sprintf(config_path, "%s\\%s", main_path, GPSP_CONFIG_FILENAME);
  #endif

  save_game_config_file();
  
  file_open(config_file, config_path, write);

  if(file_check_valid(config_file))
  {
#if defined(NSPIRE_LIBRETRO)
    u32 file_options[37];
#else
    u32 file_options[23];
#endif
    u32 i;

    file_options[0] = screen_scale;
    file_options[1] = screen_filter;
    file_options[2] = global_enable_audio;
    file_options[3] = audio_buffer_size_number;
    file_options[4] = update_backup_flag;
    file_options[5] = global_enable_analog;
    file_options[6] = analog_sensitivity_level;

#ifndef PC_BUILD
    for(i = 0; i < 16; i++)
    {
      file_options[7 + i] = gamepad_config_map[i];
    }
#endif

#if defined(NSPIRE_LIBRETRO)
    nspire_clamp_rom_buffer_size();
    file_options[23] = nspire_bios_choice % 2;
    file_options[24] = (u32)current_frameskip_type % 3;
    file_options[25] = frameskip_value;
    file_options[26] = GPSP_CFG_FS_V2_MARK;
    file_options[27] = random_skip % 2;
    file_options[28] = nspire_sprlim_choice % 2;
    file_options[29] = nspire_rtc_choice % 3;
    file_options[30] = nspire_frame_mix_choice % 2;
    file_options[31] = nspire_rom_buffer_size_choice;
    file_options[32] = nspire_lcd_cache_clean_full % 2;
    file_options[33] = nspire_fps_overlay % 2;
    file_options[34] = nspire_gba_blend_off % 2;
    file_options[35] = nspire_dynarec_ram_policy % 2;
    file_options[36] = nspire_video_renderer_choice % 2;
    file_write(config_file, file_options, 37 * sizeof(u32));
#else
    file_write_array(config_file, file_options);
#endif

    file_close(config_file);

    return 0;
  }

  return -1;
}
#endif

typedef enum
{
  MAIN_MENU,
  GAMEPAD_MENU,
  SAVESTATE_MENU,
  FRAMESKIP_MENU,
  CHEAT_MENU
} menu_enum;

u32 savestate_slot = 0;

void get_savestate_snapshot(u8 *savestate_filename)
{
#ifdef NSPIRE_BUILD
  FILE *fp = fopen((char *)savestate_filename, "rb");

  if (fp)
  {
    fclose(fp);
    print_string("Savestate present in this slot.     ", COLOR_HELP_TEXT,
     COLOR_BG, 10, 40);
  }
  else
    print_string("No savestate in this slot.          ", COLOR_HELP_TEXT,
     COLOR_BG, 10, 40);
#else
  static u16 snapshot_buffer[240 * 160];
  u8 savestate_timestamp_string[80];

  file_open(savestate_file, savestate_filename, read);

  if(file_check_valid(savestate_file))
  {
    u8 weekday_strings[7][11] =
    {
      "Sunday", "Monday", "Tuesday", "Wednesday",
      "Thursday", "Friday", "Saturday"
    };
    time_t savestate_time_flat;
    struct tm *current_time;
    file_read_array(savestate_file, snapshot_buffer);
    file_read_variable(savestate_file, savestate_time_flat);

    file_close(savestate_file);

    current_time = localtime(&savestate_time_flat);
    sprintf(savestate_timestamp_string,
     "%s  %02d/%02d/%04d  %02d:%02d:%02d                ",
     weekday_strings[current_time->tm_wday], current_time->tm_mon + 1,
     current_time->tm_mday, current_time->tm_year + 1900,
     current_time->tm_hour, current_time->tm_min, current_time->tm_sec);

    savestate_timestamp_string[40] = 0;
    print_string(savestate_timestamp_string, COLOR_HELP_TEXT, COLOR_BG,
     10, 40);
  }
  else
  {
    memset(snapshot_buffer, 0, 240 * 160 * 2);
    print_string_ext("No savestate exists for this slot.",
     0xFFFF, 0x0000, 15, 75, snapshot_buffer, 240, 0, 0, FONT_HEIGHT);
    print_string("---------- --/--/---- --:--:--          ", COLOR_HELP_TEXT,
     COLOR_BG, 10, 40);
  }

#if !defined(GP2X_BUILD) && !defined(NSPIRE_BUILD)
  blit_to_screen(snapshot_buffer, 240, 160, 230, 40);
#endif
#endif
}

void get_savestate_filename(u32 slot, u8 *name_buffer)
{
  get_savestate_filename_noshot(slot, name_buffer);
  get_savestate_snapshot(name_buffer);
}

void get_savestate_filename_noshot(u32 slot, u8 *name_buffer)
{
#if defined(NSPIRE_LIBRETRO)
  states_file_name((int)slot, (char *)name_buffer, 512);
#else
  u8 savestate_ext[16];
#ifdef NSPIRE_BUILD
  sprintf(savestate_ext, "%d.svs.tns", slot);
#else
  sprintf(savestate_ext, "%d.svs", slot);
#endif
  change_ext(gamepak_filename, name_buffer, savestate_ext);
#endif
}

#ifdef PSP_BUILD
  void _flush_cache()
  {
    invalidate_all_cache();
  }
#endif

#if defined(NSPIRE_LIBRETRO)
/* Checklist of the ten GBA buttons. */
static void gui_edit_action_mask(const char *title, unsigned short *io_mask)
{
  int cursor = 0;
  int i;

  keys_wait_release();
  for (;;)
  {
    gui_action_type act;
    char header[64];
    char summary[40];

    clear_screen(COLOR_BG);
    action_mask_name(*io_mask, summary, sizeof(summary));
    snprintf(header, sizeof(header), "%s: %s", title, summary);
    print_string_pad(header, COLOR_ACTIVE_ITEM, COLOR_BG, 10, 10, 50);
    print_string_pad("Enter toggles. Del clears. Esc is done.",
                     COLOR_HELP_TEXT, COLOR_BG, 10, 22, 52);

    for (i = 0; i < NUM_GBA_ACTIONS; i++)
    {
      char line[64];
      u16 color = (cursor == i) ? COLOR_ACTIVE_ITEM : COLOR_INACTIVE_ITEM;
      snprintf(line, sizeof(line), "%s [%c] %s",
               cursor == i ? ">" : " ",
               (*io_mask & (1u << i)) ? 'X' : ' ',
               action_names[i]);
      print_string_pad(line, color, COLOR_BG, 14, 40 + i * 12, 42);
    }
    print_string_pad(cursor == NUM_GBA_ACTIONS ? "> [Done]" : "  [Done]",
                     cursor == NUM_GBA_ACTIONS ? COLOR_ACTIVE_ITEM : COLOR_INACTIVE_ITEM,
                     COLOR_BG, 14, 40 + NUM_GBA_ACTIONS * 12 + 6, 42);
    flip_screen();
#ifdef NSPIRE_BUILD
    update_at_vblank();
#endif
    act = get_gui_input();
    if (act == CURSOR_UP)
      cursor = (cursor - 1 + NUM_GBA_ACTIONS + 1) % (NUM_GBA_ACTIONS + 1);
    else if (act == CURSOR_DOWN)
      cursor = (cursor + 1) % (NUM_GBA_ACTIONS + 1);
    else if (act == CURSOR_SELECT)
    {
      if (cursor < NUM_GBA_ACTIONS)
        *io_mask ^= (unsigned short)(1u << cursor);
      else
        break;
    }
    else if (act == CURSOR_CLEAR)
      *io_mask = 0;
    else if (act == CURSOR_BACK || act == CURSOR_EXIT)
      break;
  }
  *io_mask &= (unsigned short)((1u << NUM_GBA_ACTIONS) - 1u);
  keys_wait_release();
}
#endif

u32 menu(u16 *original_screen)
{
  u8 print_buffer[81];
  u32 clock_speed_number;
  u32 _current_option = 0;
  gui_action_type gui_action;
  menu_enum _current_menu = MAIN_MENU;
  u32 i;
  u32 repeat = 1;
  u32 return_value = 0;
  u32 first_load = 0;
  u8 savestate_ext[16];
  u8 current_savestate_filename[512];
  u8 line_buffer[80];
  u8 cheat_format_str[10][41];

  menu_type *current_menu;
  menu_option_type *current_option;
  menu_option_type *display_option;
  u32 current_option_num;

  auto void choose_menu();
  auto void clear_help();

#ifdef NSPIRE_BUILD
  u32 key_column = 0;          /* the key picked with left/right on key lines */
  u32 current_option_y = 0;
  char menu_status[80] = "";   /* what the last action did */
  unsigned touchpad_was_swipes = nspire_touchpad_swipe;
  u8 *settings_for_options[] = { "all games", "this game only" };
  u8 *touchpad_mode_options[] = { "arrows", "swipes" };
  u8 *swipe_deadzone_options[] = { "3%", "5%", "8%", "10%", "12%", "15%", "20%" };
  u8 *swipe_sensitivity_options[] = { "50%", "75%", "100%", "125%", "150%", "175%", "200%" };

  void menu_reset_keys()
  {
    keys_set_defaults(key_bindings);
    if (nspire_touchpad_swipe)
      keys_add_swipe_defaults_if_free(key_bindings);
    snprintf(menu_status, sizeof(menu_status), "Keys reset to gpSP's defaults.");
  }

  void menu_touchpad_changed()
  {
    if (nspire_touchpad_swipe && !touchpad_was_swipes)
      keys_add_swipe_defaults_if_free(key_bindings);
    touchpad_was_swipes = nspire_touchpad_swipe;
  }

  menu_option_type gamepad_config_options[] =
  {
    binding_option(ACTION_UP, 0),
    binding_option(ACTION_DOWN, 1),
    binding_option(ACTION_LEFT, 2),
    binding_option(ACTION_RIGHT, 3),
    binding_option(ACTION_A, 4),
    binding_option(ACTION_B, 5),
    binding_option(ACTION_L, 6),
    binding_option(ACTION_R, 7),
    binding_option(ACTION_START, 8),
    binding_option(ACTION_SELECT, 9),
    binding_option(ACTION_MENU, 10),
    binding_option(ACTION_FAST_FORWARD, 11),
    binding_option(ACTION_LOAD_STATE, 12),
    binding_option(ACTION_SAVE_STATE, 13),
    binding_option(ACTION_QUIT, 14),
    binding_option(ACTION_LOAD_NEWEST, 15),
    binding_option(ACTION_NEXT_SLOT, 16),
    binding_option(ACTION_PREVIOUS_SLOT, 17),
    binding_option(ACTION_SHOW_FPS, 18),
    binding_option(ACTION_RESTART, 19),
    binding_option(ACTION_BRIGHTER, 20),
    binding_option(ACTION_DIMMER, 21),
    swipe_multi_option(0, 22),
    swipe_multi_option(1, 23),
    swipe_multi_option(2, 24),
    swipe_multi_option(3, 25),
    combo_slot_option(0, 26),
    combo_slot_option(1, 27),
    combo_slot_option(2, 28),
    combo_slot_option(3, 29),
    combo_slot_option(4, 30),
    combo_slot_option(5, 31),
    combo_slot_option(6, 32),
    combo_slot_option(7, 33),
    string_selection_option(menu_touchpad_changed, "Touchpad", touchpad_mode_options,
     &nspire_touchpad_swipe, 2,
     "Arrows: a touch at the edge is a direction.\n"
     "Swipes: movement from where the finger landed.", 34),
    string_selection_option(NULL, "Swipe dead zone", swipe_deadzone_options,
     &nspire_touchpad_deadzone, SWIPE_DEADZONE_CHOICES,
     "Ignore movement smaller than this share of\n"
     "the touchpad. Each axis is measured alone.", 35),
    string_selection_option(NULL, "Swipe sensitivity", swipe_sensitivity_options,
     &nspire_touchpad_sensitivity, SWIPE_SENSITIVITY_CHOICES,
     "Higher picks up a shorter movement. 100% is\n"
     "the touchpad's own size.", 36),
    string_selection_option(nspire_config_keys_for_changed, "Keys for",
     settings_for_options, &nspire_keys_for, 2,
     "All games, or this game only: the game keeps its\n"
     "own keys.", 37),
    action_option(menu_reset_keys, NULL, "Reset keys to defaults",
     "8 5 4 6, del A, var B, scratch L, doc R, shift\n"
     "Start, ctrl Select, menu, F, L, S, Q, + and -.\n"
     "Clears swipe lists and combo slots too.", 38),
    submenu_option(NULL, "Back", "Return to the main menu.", 40)
  };


  menu_option_type analog_config_options[] =
  {
    submenu_option(NULL, "Back", "Return to the main menu.", 11)
  };
#endif
  
  u8 *gamepad_help[] =
  {
    "Up button on GBA d-pad.",
    "Down button on GBA d-pad.",
    "Left button on GBA d-pad.",
    "Right button on GBA d-pad.",
    "A button on GBA.",
    "B button on GBA.",
    "Left shoulder button on GBA.",
    "Right shoulder button on GBA.",
    "Start button on GBA.",
    "Select button on GBA.",
    "Brings up the options menu.",
    "Toggles fastforward on/off.",
    "Loads the game state from the current slot.",
    "Saves the game state to the current slot.",
    "Rapidly press/release the A button on GBA.",
    "Rapidly press/release the B button on GBA.",
    "Rapidly press/release the L shoulder on GBA.",
    "Rapidly press/release the R shoulder on GBA.",
    "Increases the volume.",
    "Decreases the volume.",
    "Displays virtual/drawn frames per second.",
    "Does nothing."
  };

  void menu_update_clock()
  {
    get_clock_speed_number();
    if (clock_speed_number < 0 || clock_speed_number >=
     sizeof(clock_speed_options) / sizeof(clock_speed_options[0]))
    {
      clock_speed = default_clock_speed;
      get_clock_speed_number();
    }
  }

  void menu_exit()
  {
    if(!first_load)
     repeat = 0;
  }

  void menu_quit()
  {
    menu_get_clock_speed();
#if defined(NSPIRE_LIBRETRO)
    if(!first_load)
      nspire_quit_game();
#endif
    save_config_file();
    quit();
  }

  void menu_load()
  {
#ifdef NSPIRE_BUILD
	u8 *file_ext[] = { ".gba.tns", ".gbz.tns", ".zip.tns", NULL };
#else
	u8 *file_ext[] = { ".gba", ".bin", ".zip", NULL };
#endif
    u8 load_filename[512];
#if defined(NSPIRE_LIBRETRO)
    if(load_file(file_ext, load_filename) != -1)
    {
      if(!first_load)
        nspire_leave_game();
      if(!nspire_start_game((char *)load_filename))
      {
        /* It said why. The last game is closed: the menu is without one. */
        gamepak_filename[0] = 0;
        first_load = 1;
        memset(original_screen, 0x00, 240 * 160 * 2);
        print_string_ext("No game loaded yet.", 0xFFFF, 0x0000,
         60, 75, original_screen, 240, 0, 0, FONT_HEIGHT);
        choose_menu(current_menu);
        return;
      }
      first_load = 0;
      return_value = 1;
      repeat = 0;
      menu_update_clock();
    }
    else
    {
      choose_menu(current_menu);
    }
#else
    save_game_config_file();
#if defined(NSPIRE_LIBRETRO)
    nspire_apply_bios();
#endif
    if(load_file(file_ext, load_filename) != -1)
    {
#if defined(NSPIRE_LIBRETRO)
       if(load_gamepak(NULL, load_filename, nspire_rtc_force_value(), FEAT_AUTODETECT,
                       SERIAL_MODE_AUTO) != 0)
#else
       if(load_gamepak(load_filename) == -1)
#endif
       {
         quit();
       }
       reset_gba();
#if defined(NSPIRE_LIBRETRO)
       nspire_frameskip_reset();
       nspire_load_cartridge_backup();
#endif
#ifdef NSPIRE_BUILD
       //change_ext(gamepak_filename, load_filename, ".qsv.tns");
       //load_state(load_filename);
       //remove(load_filename);
	   	if (quick_save_slot)
		{
			u8 current_savestate_filename[512];
			get_savestate_filename_noshot(quick_save_slot - 1, current_savestate_filename);
			if (load_state(current_savestate_filename))
			  quick_save_slot = 0;
		}
#endif
       return_value = 1;
       repeat = 0;
#if defined(NSPIRE_LIBRETRO)
       instruction_count = 0;
       reg[OAM_UPDATED] = 1;
#else
       reg[CHANGED_PC_STATUS] = 1;
#endif
       menu_update_clock();
    }
    else
    {
      choose_menu(current_menu);
    }
#endif
  }

  void menu_restart()
  {
    if(!first_load)
    {
#if defined(NSPIRE_LIBRETRO)
      nspire_apply_bios();
#endif
      reset_gba();
#if defined(NSPIRE_LIBRETRO)
      nspire_frameskip_reset();
      instruction_count = 0;
      reg[OAM_UPDATED] = 1;
      nspire_osd_message("Game restarted");
#else
      reg[CHANGED_PC_STATUS] = 1;
#endif
      return_value = 1;
      repeat = 0;
    }
  }

  void menu_change_state()
  {
    get_savestate_filename(savestate_slot, current_savestate_filename);
  }

#if defined(NSPIRE_LIBRETRO)
  void menu_save_state()
  {
    int slot;

    if(first_load)
      return;
    slot = nspire_save_target();
    if(slot < 0)
      snprintf(menu_status, sizeof(menu_status), "All 1000 slots are used.");
    else if(states_save(slot))
    {
      savestate_slot = (u32)slot;
      snprintf(menu_status, sizeof(menu_status), "Saved slot %03d.", slot);
    }
    else
      snprintf(menu_status, sizeof(menu_status), "Couldn't save slot %03d. Out of space?", slot);
  }

  void menu_load_state()
  {
    int result;

    if(first_load)
      return;
    result = states_load((int)savestate_slot);
    if(result == 1)
    {
      nspire_osd_message("Loaded slot %03d", (int)savestate_slot);
      nspire_frameskip_reset();
      return_value = 1;
      repeat = 0;
    }
    else
      snprintf(menu_status, sizeof(menu_status), result == 0 ? "Slot %03d is empty." :
               "Couldn't load slot %03d.", (int)savestate_slot);
  }

  void menu_speed_test()
  {
    if(!first_load)
      nspire_speed_test();
  }

  void menu_save_manager()
  {
    if(!first_load)
      nspire_save_manager();
  }

  void menu_rom_manager()
  {
    nspire_rom_manager();
  }
#else
  void menu_save_state()
  {
    if(!first_load)
    {
      get_savestate_filename_noshot(savestate_slot,
       current_savestate_filename);
      save_state(current_savestate_filename, original_screen);
    }
    menu_change_state();
  }

  void menu_load_state()
  {
    if(!first_load)
    {
      if (load_state(current_savestate_filename))
      {
        return_value = 1;
        repeat = 0;
#if defined(NSPIRE_LIBRETRO)
        nspire_frameskip_reset();
#endif
      }
    }
  }
#endif

  void menu_load_state_file()
  {
#ifdef NSPIRE_BUILD
    u8 *file_ext[] = { ".svs.tns", NULL };
#else
    u8 *file_ext[] = { ".svs", NULL };
#endif
    u8 load_filename[512];
    if(load_file(file_ext, load_filename) != -1)
    {
      if (load_state(load_filename))
      {
        return_value = 1;
        repeat = 0;
#if defined(NSPIRE_LIBRETRO)
        nspire_frameskip_reset();
#endif
      }
    }
    else
    {
      choose_menu(current_menu);
    }
  }

  void menu_fix_gamepad_help()
  {
#if !defined(PC_BUILD) && !defined(NSPIRE_BUILD)
    clear_help();
    current_option->help_string =
     gamepad_help[gamepad_config_map[
     gamepad_config_line_to_button[current_option_num]]];
#endif
  }

  void submenu_graphics_sound()
  {

  }

  void submenu_cheats_misc()
  {

  }

  void submenu_gamepad()
  {
  }

  void submenu_analog()
  {

  }

  void submenu_savestate()
  {
    print_string("Savestate options:", COLOR_ACTIVE_ITEM, COLOR_BG, 10, 70);
    menu_change_state();
  }

  void submenu_about()
  {
    print_string("gpsp-libretro for nSpire (Alpha v0.4.1)", COLOR_ACTIVE_ITEM, COLOR_BG, 10, 10);
    print_string("by andymcca", COLOR_ACTIVE_ITEM, COLOR_BG, 10, 20);
#if defined(NSPIRE_LIBRETRO)
    print_string("+ tear-free CX II output, 1000 save slots,", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 40);
    print_string("  compressed states and ROMs (.gbz), hotkeys,", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 50);
    print_string("  settings per game, CPU speed, brightness,", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 60);
    print_string("  number keys and your own order in the list", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 70);
#endif
    print_string("Credits:", COLOR_ACTIVE_ITEM, COLOR_BG, 10, 90);
    print_string("Exophase", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 100);
    print_string("David G.F.", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 110);
    print_string("calc84maniac", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 120);
    print_string("Vogtinator", COLOR_INACTIVE_ITEM, COLOR_BG, 10, 130);
  }

  void submenu_main()
  {
    if (*gamepak_filename)
	{
#if defined(NSPIRE_LIBRETRO)
		/* The ROM's file name: its path is absolute, and too long. */
		const char *slash = strrchr(gamepak_filename, '/');
		snprintf((char *)print_buffer, sizeof(print_buffer), "%s", slash ? slash + 1 : gamepak_filename);
#else
		strncpy(print_buffer, gamepak_filename, 80);
#endif
		print_string(print_buffer, COLOR_ROM_INFO, COLOR_BG, 10, 10);
		sprintf(print_buffer, "%s  %s  %s", gamepak_title,
		 gamepak_code, gamepak_maker);
		print_string(print_buffer, COLOR_ROM_INFO, COLOR_BG, 10, 20);

		get_savestate_filename_noshot(savestate_slot,
		 current_savestate_filename);
	}
	else
	{
		print_string("gpsp-libretro for nspire (Alpha v0.4.1)",
		 COLOR_ACTIVE_ITEM, COLOR_BG, 10, 10);
	}
  }

  u8 *yes_no_options[] = { "no", "yes" };
  u8 *enable_disable_options[] = { "disabled", "enabled" };

#if !defined(GP2X_BUILD)
  u8 *frameskip_options[] = { "automatic", "manual", "off" };
#endif
  u8 *frameskip_variation_options[] = { "uniform", "random" };

#if defined(NSPIRE_LIBRETRO)
  u8 *bios_source_options[] = {
    "Built-in BIOS",
    "gba_bios.bin.tns (official)"
  };
  u8 *nspire_sprlim_options[] = { "disabled", "enabled" };
  u8 *nspire_rtc_options[] = { "auto", "enabled", "disabled" };
  u8 *nspire_frame_mix_options[] = { "disabled", "enabled" };
  u8 *nspire_lcd_cache_options[] = { "Range D-clean", "Full clean_dcache" };
  u8 *nspire_fps_overlay_options[] = { "off", "on", "detailed" };
  u8 *nspire_gba_blend_options[] = { "On", "Off (fast)" };
  u8 *nspire_video_renderer_options[] = { "libretro video.cc", "legacy old_video/video.c" };
  u8 *nspire_dynarec_ram_policy_options[] = { "Partial SMC (default)", "Classic (full flush on SMC)" };
  u8 *brightness_options[] = { "calculator's", "1 (dimmest)", "2", "3", "4", "5", "6", "7",
   "8", "9", "10 (brightest)" };
  u8 *cpu_speed_options[] = { "normal", "408 MHz", "420 MHz", "432 MHz", "444 MHz", "456 MHz",
   "468 MHz", "480 MHz", "492 MHz", "504 MHz" };
  u8 *off_on_options[] = { "off", "on" };
#endif

#ifndef PSP_BUILD
  u8 *audio_buffer_options[] =
  {
    "16 bytes", "32 bytes", "64 bytes",
    "128 bytes", "256 bytes", "512 bytes", "1024 bytes", "2048 bytes",
    "4096 bytes", "8192 bytes", "16284 bytes"
  };
#else
  u8 *audio_buffer_options[] =
  {
    "3072 bytes", "4096 bytes", "5120 bytes", "6144 bytes", "7168 bytes",
    "8192 bytes", "9216 bytes", "10240 bytes", "11264 bytes", "12288 bytes"
  };

#endif

  u8 *update_backup_options[] = { "Exit only", "Automatic" };

  u8 *gamepad_config_buttons[] =
  {
    "UP",
    "DOWN",
    "LEFT",
    "RIGHT",
    "A",
    "B",
    "L",
    "R",
    "START",
    "SELECT",
    "MENU",
    "FASTFORWARD",
    "LOAD STATE",
    "SAVE STATE",
    "RAPIDFIRE A",
    "RAPIDFIRE B",
    "RAPIDFIRE L",
    "RAPIDFIRE R",
    "VOLUME UP",
    "VOLUME DOWN",
    "DISPLAY FPS",
    "NOTHING"
  };

  // Marker for help information, don't go past this mark (except \n)------*
  menu_option_type graphics_sound_options[] =
  {
    string_selection_option(NULL, "Display scaling", scale_options,
     (u32 *)(&screen_scale),
     sizeof(scale_options) / sizeof(scale_options[0]),
#if !defined(GP2X_BUILD) && !defined(NSPIRE_BUILD)
     "Determines how the GBA screen is resized in relation to the entire\n"
     "screen. Select unscaled 3:2 for GBA resolution, scaled 3:2 for GBA\n"
     "aspect ratio scaled to fill the height of the PSP screen, and\n"
     "fullscreen to fill the entire PSP screen."
#endif
#if defined(NSPIRE_LIBRETRO)
     "Scaled/unblended is the sharp 320x214 picture.\n"
     "Blended ones are smoother and slower; unscaled is\n"
     "the GBA's own 240x160 in the middle."
#endif
     "", 2),
#if defined(NSPIRE_LIBRETRO)
    string_selection_option(nspire_brightness_apply, "Screen brightness",
     brightness_options, &nspire_brightness, BRIGHTNESS_LEVELS + 1,
     "The backlight while gpSP runs (CX II); the\n"
     "calculator's own comes back when gpSP closes.\n"
     "In a game: + and - (Configure input).", 3),
#endif
#if !defined(GP2X_BUILD) && !defined(NSPIRE_BUILD)
    string_selection_option(NULL, "Screen filtering", yes_no_options,
     (u32 *)(&screen_filter), 2,
     "Determines whether or not bilinear filtering should be used when\n"
     "scaling the screen. Selecting this will produce a more even and\n"
     "smooth image, at the cost of being blurry and having less vibrant\n"
     "colors.", 3),
#endif
#if defined(NSPIRE_BUILD)
    string_selection_option(NULL, "Frameskip type", frameskip_options,
     (u32 *)(&current_frameskip_type), 3,
     "Automatic: skip drawing only when the game falls\n"
     "behind. Manual: draw 1 in N+1. Off: draw them all.", 5),
    numeric_selection_option(NULL, "Frameskip value", &frameskip_value, 100,
     "Automatic: the most frames skipped in a row.\n"
     "Manual: N frames skipped for each one drawn.", 6),
    string_selection_option(NULL, "Frameskip variation",
     frameskip_variation_options, &random_skip, 2,
     "Manual frameskip at random times: can stop\n"
     "things that flicker from disappearing.", 7),
#if defined(NSPIRE_LIBRETRO)
    string_selection_option(NULL, "LCD framebuffer cache",
     nspire_lcd_cache_options, &nspire_lcd_cache_clean_full, 2,
     "Landscape LCDs only (not the CX II): how a frame\n"
     "leaves the cache before it is shown. Full if you\n"
     "see corruption.", 8),
    string_selection_option(NULL, "Show FPS counter",
     nspire_fps_overlay_options, &nspire_fps_overlay, 3,
     "Frames drawn a second / the most there can be.\n"
     "Detailed: ms per frame running the game, drawing,\n"
     "showing it (also logged to gpsp_perf.txt).", 9),
    string_selection_option(NULL, "GBA blending (BLDCNT)",
     nspire_gba_blend_options, &nspire_gba_blend_off, 2,
     "On: see-through layers, fades and sprites drawn\n"
     "properly. Off (fast): skips them (modes 0-2 and\n"
     "4); wrong in many games.", 10),
    string_selection_option(NULL, "Video renderer",
     nspire_video_renderer_options, &nspire_video_renderer_choice, 2,
     "libretro video.cc is the default renderer.\n"
     "legacy old_video/video.c can be faster in some\n"
     "scenes (uses video_blend.S).", 11),
#endif
#elif !defined(GP2X_BUILD)
    string_selection_option(NULL, "Frameskip type", frameskip_options,
     (u32 *)(&current_frameskip_type), 3,
#if !defined(NSPIRE_BUILD)
     "Determines what kind of frameskipping to use.\n"
     "Frameskipping may improve emulation speed of many games.\n"
#endif
     "Off: Do not skip any frames.\n"
     "Auto: Skip up to N frames (see next opt) as needed.\n"
     "Manual: Always render only 1 out of N + 1 frames."
     , 5),
    numeric_selection_option(NULL, "Frameskip value", &frameskip_value, 100,
#if !defined(NSPIRE_BUILD)
     "For auto frameskip, determines the maximum number of frames that\n"
     "are allowed to be skipped consecutively.\n"
     "For manual frameskip, determines the number of frames that will\n"
     "always be skipped."
#endif
     "", 6),
    string_selection_option(NULL, "Frameskip variation",
     frameskip_variation_options, &random_skip, 2,
#if !defined(NSPIRE_BUILD)
     "If objects in the game flicker at a regular rate certain manual\n"
     "frameskip values may cause them to normally disappear. Change this\n"
     "value to 'random' to avoid this. Do not use otherwise, as it tends to\n"
     "make the image quality worse, especially in high motion games."
#endif
     "", 7),
#endif
#ifndef NSPIRE_BUILD
    string_selection_option(NULL, "Audio output", yes_no_options,
     &global_enable_audio, 2,
     "Select 'no' to turn off all audio output. This will\n"
     "not result in a significant change in performance.", 9),
#ifndef PSP_BUILD
    string_selection_option(NULL, "Audio buffer", audio_buffer_options,
             &audio_buffer_size_number, 11,
#else
    string_selection_option(NULL, "Audio buffer", audio_buffer_options,
             &audio_buffer_size_number, 10,
#endif

#ifdef PSP_BUILD
     "Set the size (in bytes) of the audio buffer. Larger values may result\n"
     "in slightly better performance at the cost of latency; the lowest\n"
     "value will give the most responsive audio.\n"
     "This option requires gpSP to be restarted before it will take effect.",
#else
     "Set the size (in bytes) of the audio buffer.\n"
     "This option requires gpSP restart to take effect.",
#endif
     10),
#endif
#if defined(NSPIRE_LIBRETRO)
    action_option(menu_speed_test, NULL, "Run speed test",
     "Runs the game from here for 20 seconds, three\n"
     "ways, and shows where the time goes; then it's\n"
     "put back. Results: gpsp_results.txt.", 13),
    submenu_option(NULL, "Back", "Return to the main menu.", 15)
#else
    submenu_option(NULL, "Back", "Return to the main menu.", 12)
#endif
  };

  make_menu(graphics_sound, submenu_graphics_sound, NULL);

#if defined(NSPIRE_LIBRETRO)
  menu_option_type cheats_misc_options[] =
  {
    cheat_option(0),
    cheat_option(1),
    cheat_option(2),
    cheat_option(3),
    cheat_option(4),
    cheat_option(5),
    cheat_option(6),
    cheat_option(7),
    cheat_option(8),
    cheat_option(9),
    string_selection_option(NULL, "Auto-increment slot on save", off_on_options,
     &nspire_auto_increment, 2,
     "On: every save goes to a new slot after the\n"
     "highest, so nothing is overwritten. Off: saves\n"
     "go to the slot picked.", 10),
    string_selection_option(NULL, "Save a state when leaving", off_on_options,
     &nspire_save_on_exit, 2,
     "Save+Exit, Exit gpSP and Load new game save a\n"
     "state first, to carry on from there next time.", 11),
    string_selection_option(NULL, "Load newest state on start", off_on_options,
     &nspire_load_on_start, 2,
     "Starting a game loads its newest state. To start\n"
     "without it once: shift+enter in the file list.", 12),
    string_selection_option(NULL, "Update backup",
     update_backup_options, &update_backup_flag, 2,
     "When in-game saves are written: automatically, a\n"
     "moment after the game saves, or only when leaving.", 13),
    string_selection_option(NULL, "Compress saves", off_on_options,
     &nspire_compress_saves, 2,
     "New states and in-game saves (.sav.gz) packed\n"
     "small. Off: as they are, for other emulators.\n"
     "Save manager (main menu) changes old ones.", 14),
    submenu_option(NULL, "Back", "Return to the main menu.", 15)
  };
#else
  menu_option_type cheats_misc_options[] =
  {
    cheat_option(0),
    cheat_option(1),
    cheat_option(2),
    cheat_option(3),
    cheat_option(4),
    cheat_option(5),
    cheat_option(6),
    cheat_option(7),
    cheat_option(8),
    cheat_option(9),
#ifndef NSPIRE_BUILD
    string_selection_option(NULL, "Clock speed",
     clock_speed_options, &clock_speed_number,
     sizeof(clock_speed_options) / sizeof(clock_speed_options[0]),
     "Change the clock speed of the device. Higher clock\n"
     "speed will yield better performance, but will drain\n"
     "battery life further.", 11),
#endif
    string_selection_option(NULL, "Update backup",
     update_backup_options, &update_backup_flag, 2,
#ifdef GP2X_BUILD
     "Determines when in-game save files should be\n"
     "written back to SD card.",
#elif defined(NSPIRE_BUILD)
	 "Determines when in-game save files should be\n"
	 "written back to internal storage memory.\n",
#else
     "Determines when in-game save files should be written back to\n"
     "memstick. If set to 'automatic' writebacks will occur shortly after\n"
     "the game's backup is altered. On 'exit only' it will only be written\n"
     "back when you exit from this menu (NOT from using the home button).\n"
     "Use the latter with extreme care.",
#endif
     12),
    submenu_option(NULL, "Back", "Return to the main menu.", 14)
  };
#endif

  make_menu(cheats_misc, submenu_cheats_misc, NULL);

  menu_option_type savestate_options[] =
  {
    numeric_selection_action_hide_option(menu_load_state, menu_change_state,
     "Load savestate from current slot", &savestate_slot, 10,
     "Select to load the game state from the current slot\n"
     "for this game.\n"
     "Press left + right to change the current slot.", 6),
    numeric_selection_action_hide_option(menu_save_state, menu_change_state,
     "Save savestate to current slot", &savestate_slot, 10,
     "Select to save the game state to the current slot\n"
     "for this game.\n"
     "Press left + right to change the current slot.", 7),
    numeric_selection_action_hide_option(menu_load_state_file,
      menu_change_state,
     "Load savestate from file", &savestate_slot, 10,
     "Restore gameplay from a savestate file.\n"
     "Note: The same file used to save the state must be\n"
     "present.\n", 9),
    numeric_selection_option(menu_change_state,
     "Current savestate slot", &savestate_slot, 10,
     "Change the current savestate slot.\n", 11),
    submenu_option(NULL, "Back", "Return to the main menu.", 13)
  };

  make_menu(savestate, submenu_savestate, NULL);

  menu_option_type about_options[] =
  {
    submenu_option(NULL, "Back", "Return to the main menu.", 13)
  };

  make_menu(about, submenu_about, NULL);

#if defined(NSPIRE_LIBRETRO)
  void submenu_emulator()
  {
    nspire_emulator_options_apply();
  }

  menu_option_type emulator_options[] =
  {
    string_selection_option(nspire_config_settings_for_changed, "Settings for",
     settings_for_options, &nspire_settings_for, 2,
     "All games, or this game only: the game keeps\n"
     "its own graphics, emulator and save state\n"
     "options.", 0),
    numeric_selection_option(nspire_clamp_rom_buffer_size, "ROM buffer size (MiB)",
     &nspire_rom_buffer_size_choice, 33,
     "ROM page cache in MiB (2 to 32). Lower values\n"
     "use less RAM but read the ROM file more often.", 2),
    string_selection_option(NULL, "GBA BIOS source", bios_source_options,
     &nspire_bios_choice, 2,
     "Built-in: the open BIOS in gpSP. File: the\n"
     "official gba_bios.bin.tns, in zgpSP_saves.\n"
     "Applies when a game is loaded or restarted.", 3),
    string_selection_option(NULL, "No sprite limit", nspire_sprlim_options,
     &nspire_sprlim_choice, 2,
     "Disabled: the 128 sprite cap, as on a GBA.\n"
     "Enabled: all sprites drawn (may be slower).\n"
     "As RetroArch's gpsp_sprlim.", 4),
    string_selection_option(NULL, "RTC", nspire_rtc_options,
     &nspire_rtc_choice, 3,
     "The cartridge's real-time clock (gpsp_rtc).\n"
     "Auto: for the games known to have one. Applies\n"
     "the next time a game is loaded.", 5),
    string_selection_option(NULL, "Interframe blending", nspire_frame_mix_options,
     &nspire_frame_mix_choice, 2,
     "Blends each frame with the one before, like the\n"
     "GBA LCD's ghosting (libretro gpsp_frame_mixing).", 6),
    string_selection_option(nspire_dynarec_ram_policy_menu_hook, "Dynarec RAM / SMC",
     nspire_dynarec_ram_policy_options, &nspire_dynarec_ram_policy, 2,
     "Partial SMC: code a game rewrites in RAM is\n"
     "retranslated alone (faster). Classic: all of it\n"
     "is flushed, as in old gpSP (slower).", 7),
    string_selection_option(NULL, "CPU speed (CX II)", cpu_speed_options,
     &nspire_cpu_speed, CPU_SPEEDS,
     "Raised while a game runs; normal is 396 MHz (288\n"
     "on USB). Try 432 first: faster ones can freeze.\n"
     "A speed that froze goes back to normal next start.", 9),
    submenu_option(NULL, "Back", "Return to the main menu.", 13)
  };

  make_menu(emulator, submenu_emulator, NULL);
#endif

#ifdef PSP_BUILD

  menu_option_type gamepad_config_options[] =
  {
    gamepad_config_option("D-pad up     ", 0),
    gamepad_config_option("D-pad down   ", 1),
    gamepad_config_option("D-pad left   ", 2),
    gamepad_config_option("D-pad right  ", 3),
    gamepad_config_option("Circle       ", 4),
    gamepad_config_option("Cross        ", 5),
    gamepad_config_option("Square       ", 6),
    gamepad_config_option("Triangle     ", 7),
    gamepad_config_option("Left Trigger ", 8),
    gamepad_config_option("Right Trigger", 9),
    gamepad_config_option("Start        ", 10),
    gamepad_config_option("Select       ", 11),
    submenu_option(NULL, "Back", "Return to the main menu.", 13)
  };


  menu_option_type analog_config_options[] =
  {
    analog_config_option("Analog up   ", 0),
    analog_config_option("Analog down ", 1),
    analog_config_option("Analog left ", 2),
    analog_config_option("Analog right", 3),
    string_selection_option(NULL, "Enable analog", yes_no_options,
     &global_enable_analog, 2,
     "Select 'no' to block analog input entirely.", 7),
    numeric_selection_option(NULL, "Analog sensitivity",
     &analog_sensitivity_level, 10,
     "Determine sensitivity/responsiveness of the analog input.\n"
     "Lower numbers are less sensitive.", 8),
    submenu_option(NULL, "Back", "Return to the main menu.", 11)
  };

#endif

#ifdef GP2X_BUILD

  menu_option_type gamepad_config_options[] =
  {
    gamepad_config_option("D-pad up     ", 0),
    gamepad_config_option("D-pad down   ", 1),
    gamepad_config_option("D-pad left   ", 2),
    gamepad_config_option("D-pad right  ", 3),
    gamepad_config_option("A            ", 4),
    gamepad_config_option("B            ", 5),
    gamepad_config_option("X            ", 6),
    gamepad_config_option("Y            ", 7),
    gamepad_config_option("Left Trigger ", 8),
    gamepad_config_option("Right Trigger", 9),
#ifdef WIZ_BUILD
    gamepad_config_option("Menu         ", 10),
#else
    gamepad_config_option("Start        ", 10),
#endif
    gamepad_config_option("Select       ", 11),
#ifndef WIZ_BUILD
    gamepad_config_option("Stick Push   ", 12),
#endif
    submenu_option(NULL, "Back", "Return to the main menu.", 14)
  };


  menu_option_type analog_config_options[] =
  {
    submenu_option(NULL, "Back", "Return to the main menu.", 11)
  };

#endif

#ifdef PC_BUILD

  menu_option_type gamepad_config_options[] =
  {
    submenu_option(NULL, "Back", "Return to the main menu.", 13)
  };

  menu_option_type analog_config_options[] =
  {
    submenu_option(NULL, "Back", "Return to the main menu.", 11)
  };

#endif

  make_menu(gamepad_config, submenu_gamepad, NULL);
  make_menu(analog_config, submenu_analog, NULL);

  menu_option_type main_options[] =
  {
#ifdef NSPIRE_BUILD
	submenu_option(&graphics_sound_menu, "Graphics options",
     "Display scale, frameskip, FPS counter, blending,\n"
     "renderer, and the speed test.", 0),
#if defined(NSPIRE_LIBRETRO)
	submenu_option(&emulator_menu, "Emulator options",
     "Settings for all games or this one, BIOS, sprite\n"
     "limit, RTC, interframe blending, CPU speed.", 1),
#endif
#else
    submenu_option(&graphics_sound_menu, "Graphics and Sound options",
     "Select to set display parameters and frameskip\n"
     "behavior, audio on/off, buffer size, and filtering.", 0),
#endif
    numeric_selection_action_option(menu_load_state, NULL,
     "Load state from slot", &savestate_slot, MENU_SLOTS,
     "Loads the slot picked. Left/right pick the slot\n"
     "(hold them to go faster).", LOAD_SLOT_LINE),
    numeric_selection_action_option(menu_save_state, NULL,
     "Save state to slot", &savestate_slot, MENU_SLOTS,
     "Saves the game here: to a new slot with auto-\n"
     "increment on (Cheats and Misc), else to the\n"
     "slot picked.", SAVE_SLOT_LINE),
#if defined(NSPIRE_LIBRETRO)
    action_option(menu_save_manager, NULL, "Save manager",
     "This game's in-game save and states, their sizes:\n"
     "compress, uncompress or delete them.", 5),
#endif
#if !defined(NSPIRE_BUILD)
    submenu_option(&savestate_menu, "Savestate options",
     "Select to enter a menu for loading, saving, and\n"
     "viewing the currently active savestate for this game\n"
     "(or to load a savestate file from another game)", 4),
#endif
	submenu_option(&gamepad_config_menu, "Configure input",
     "Two keys per button or hotkey, touchpad arrows\n"
     "or swipes, and one input holding several buttons.", 6),
#if !defined(GP2X_BUILD) && !defined(NSPIRE_BUILD)
    submenu_option(&analog_config_menu, "Configure analog input",
     "Select to change the in-game behavior of the PSP analog nub.", 7),
#endif
    submenu_option(&cheats_misc_menu, "Cheats and Miscellaneous options",
     "Cheats, save state options (auto-increment, save\n"
     "when leaving, load on start), in-game saves,\n"
     "compressing saves.", 8),
#if defined(NSPIRE_LIBRETRO)
    action_option(menu_rom_manager, NULL, "ROM manager",
     "This folder's games: compress them into .zip\n"
     "files or back into .gba files. Saves stay.", 9),
#endif
    submenu_option(&about_menu, "About",
     "View credits and acknowledgements.", 10),
    action_option(menu_load, NULL, "Load new game",
     "Select to load a new game\n"
     "(will exit a game if currently playing).", 11),
    action_option(menu_restart, NULL, "Restart game",
     "Select to reset the GBA with the current game\n"
     "loaded.", 12),
    action_option(menu_exit, NULL, "Return to game",
     "Select to exit this menu and resume gameplay.", 13),
    action_option(menu_quit, NULL, "Exit gpSP",
     "Select to exit gpSP and return to the menu.", 15)
  };

  make_menu(main, submenu_main, NULL);

#if defined(NSPIRE_LIBRETRO)
  /* The slot lines say what selecting them does: which slot a save goes to
   * (a new one with auto-increment on) and whether the slot to load from
   * holds a state. */
  void refresh_slot_lines()
  {
    static char load_line[64], save_line[64];
    u32 n;

    for(n = 0; n < sizeof(main_options) / sizeof(main_options[0]); n++)
    {
      menu_option_type *o = &main_options[n];
      if(o->line_number == LOAD_SLOT_LINE)
      {
        snprintf(load_line, sizeof(load_line), "Load state from slot: %%03d%s",
         states_exists((int)savestate_slot) ? "" : " (empty)");
        o->display_string = load_line;
      }
      else if(o->line_number == SAVE_SLOT_LINE)
      {
        if(nspire_auto_increment)
        {
          int target = nspire_save_target();
          if(target < 0)
            snprintf(save_line, sizeof(save_line), "Save state: all 1000 slots are used");
          else
            snprintf(save_line, sizeof(save_line), "Save state to new slot %03d", target);
          o->option_type = ACTION_OPTION;
        }
        else
        {
          snprintf(save_line, sizeof(save_line), "Save state to slot: %%03d%s",
           states_exists((int)savestate_slot) ? " (replaces it)" : "");
          o->option_type = NUMBER_SELECTION_OPTION | ACTION_OPTION;
        }
        o->display_string = save_line;
      }
    }
  }
#endif

  void choose_menu(menu_type *new_menu)
  {
    if(new_menu == NULL)
      new_menu = &main_menu;

    clear_screen(COLOR_BG);

#if !defined(GP2X_BUILD) && !defined(NSPIRE_BUILD)
    blit_to_screen(original_screen, 240, 160, 230, 40);
#endif

    current_menu = new_menu;
    current_option = new_menu->options;
    current_option_num = 0;
    if(current_menu->init_function)
     current_menu->init_function();
  }

  void clear_help()
  {
    for(i = 0; i < 6; i++)
    {
      print_string_pad(" ", COLOR_BG, COLOR_BG, 8, 210 + (i * 10), 70);
    }
  }

  void print_help_wrapped(const char *text)
  {
    const u32 max_lines = 6;
    const u32 max_cols = 50;
    u32 line_index = 0;
    const char *p = text ? text : "";

#if defined(NSPIRE_HOST)
    /* For the PC tests: the screen has room for 3 lines of 50 under the
     * options, so a help text must fit them. */
    {
      const char *q;
      u32 lines = 1, col = 0, too_wide = 0;
      for (q = p; *q; q++)
        if (*q == '\n')
        {
          lines++;
          col = 0;
        }
        else if (++col > max_cols)
          too_wide = 1;
      if (q > p && q[-1] == '\n')
        lines--;
      if (too_wide || lines > 3)
      {
        fprintf(stderr, "gpsp-host: help text too long: ");
        for (q = p; *q; q++)
          fputc(*q == '\n' ? '|' : *q, stderr);
        fputc('\n', stderr);
      }
    }
#endif

    while (*p && line_index < max_lines)
    {
      const char *start = p;
      u32 len = 0;
      s32 last_space = -1;
      char line_buffer_local[96];

      if (*p == '\n')
      {
        line_index++;
        p++;
        continue;
      }

      while (p[len] && p[len] != '\n' && len < max_cols)
      {
        if (p[len] == ' ')
          last_space = (s32)len;
        len++;
      }

      if (p[len] && p[len] != '\n' && len == max_cols && last_space >= 0)
      {
        len = (u32)last_space;
      }

      while (len && start[len - 1] == ' ')
        len--;

      memcpy(line_buffer_local, start, len);
      line_buffer_local[len] = 0;
      print_string_pad(line_buffer_local, COLOR_HELP_TEXT, COLOR_BG, 8,
       210 + (line_index * 10), 70);
      line_index++;

      p = start + len;
      while (*p == ' ')
        p++;
      if (*p == '\n')
        p++;
    }

    while (line_index < max_lines)
    {
      print_string_pad(" ", COLOR_BG, COLOR_BG, 8, 210 + (line_index * 10), 70);
      line_index++;
    }
  }

#if defined(NSPIRE_LIBRETRO)
  /* The menu's options call the functions above through trampolines, code
   * GCC wrote onto this frame of the stack as data on the way in. The menu
   * opens from more than one stack depth, so they must reach memory and
   * the instruction cache can't keep older ones. */
  nspire_sync_written_code();
#endif

  menu_update_clock();
  video_resolution_large();

#ifndef NSPIRE_BUILD
#ifndef GP2X_BUILD
  SDL_LockMutex(sound_mutex);
#endif
  SDL_PauseAudio(1);

#ifndef GP2X_BUILD
  SDL_UnlockMutex(sound_mutex);
#endif
#endif

  if(gamepak_filename[0] == 0)
  {
    first_load = 1;
    memset(original_screen, 0x00, 240 * 160 * 2);
    print_string_ext("No game loaded yet.", 0xFFFF, 0x0000,
     60, 75,original_screen, 240, 0, 0, FONT_HEIGHT);
  }

  choose_menu(&main_menu);

  for(i = 0; i < 10; i++)
  {
    if(i >= num_cheats)
    {
      sprintf(cheat_format_str[i], "cheat %d (none loaded)", i);
    }
    else
    {
      sprintf(cheat_format_str[i], "cheat %d (%s): %%s", i,
       cheats[i].cheat_name);
    }
  }

  //current_menu->init_function();

  while(repeat)
  {
    u32 scroll = 0;
    display_option = current_menu->options;
#ifdef NSPIRE_BUILD
	clear_screen(COLOR_BG);
#endif
#if defined(NSPIRE_LIBRETRO)
    refresh_slot_lines();
    /* Menus longer than the screen scroll to keep the selected line in view. */
    {
      u32 max_line = 0, line_now = current_option->line_number;
      for(i = 0; i < current_menu->num_options; i++)
        if(current_menu->options[i].line_number > max_line)
          max_line = current_menu->options[i].line_number;
      if(max_line > MENU_LAST_LINE && line_now > MENU_LAST_LINE / 2)
      {
        scroll = line_now - MENU_LAST_LINE / 2;
        if(scroll > max_line - MENU_LAST_LINE)
          scroll = max_line - MENU_LAST_LINE;
      }
    }
#endif
    current_menu->init_function();
	for(i = 0; i < current_menu->num_options; i++, display_option++)
    {
      u32 line = display_option->line_number;

#if defined(NSPIRE_LIBRETRO)
      if(line < scroll || line - scroll > MENU_LAST_LINE)
        continue;
      line -= scroll;
      if(display_option->option_type & KEY_BINDING_OPTION)
      {
        /* "A                  del       [7]": the picked key in brackets,
         * the second keys lined up. */
        char first[24], second[24], cell[32];
        u32 action = display_option->num_options;
        int picked = display_option == current_option;
        binding_name(key_bindings[action][0], first, sizeof(first));
        binding_name(key_bindings[action][1], second, sizeof(second));
        snprintf(cell, sizeof(cell), "%s%s%s", picked && key_column == 0 ? "[" : " ", first,
         picked && key_column == 0 ? "]" : " ");
        snprintf((char *)line_buffer, sizeof(line_buffer), "%-17s %-11s%s%s%s",
         action_names[action], cell,
         picked && key_column == 1 ? "[" : " ", second, picked && key_column == 1 ? "]" : " ");
      }
      else if(display_option->option_type & SWIPE_MULTI_OPTION)
      {
        char mask_str[32];
        u32 dir = display_option->num_options;
        int picked = display_option == current_option;
        action_mask_name(swipe_action_masks[dir], mask_str, sizeof(mask_str));
        snprintf((char *)line_buffer, sizeof(line_buffer), "%-17s %s%s%s",
         swipe_dir_names[dir], picked ? "[" : " ", mask_str, picked ? "]" : " ");
      }
      else if(display_option->option_type & COMBO_SLOT_OPTION)
      {
        char trig_str[24], mask_str[28], label[20], cell[32];
        u32 slot = display_option->num_options;
        int picked = display_option == current_option;
        snprintf(label, sizeof(label), "Combo %u", (unsigned)(slot + 1));
        binding_name(combo_bindings[slot].trigger, trig_str, sizeof(trig_str));
        action_mask_name(combo_bindings[slot].action_mask, mask_str, sizeof(mask_str));
        snprintf(cell, sizeof(cell), "%s%s%s",
         picked && key_column == 0 ? "[" : " ", trig_str,
         picked && key_column == 0 ? "]" : " ");
        snprintf((char *)line_buffer, sizeof(line_buffer), "%-17s %-11s%s%s%s",
         label, cell, picked && key_column == 1 ? "[" : " ", mask_str,
         picked && key_column == 1 ? "]" : " ");
      }
      else
#endif
      if(display_option->option_type & NUMBER_SELECTION_OPTION)
      {
        sprintf(line_buffer, display_option->display_string,
         *(display_option->current_option));
      }
      else

      if(display_option->option_type & (STRING_SELECTION_OPTION))
      {
        sprintf(line_buffer, display_option->display_string,
         ((char **)display_option->options)[*(display_option->current_option)]);
      }
      else
      {
        strcpy(line_buffer, display_option->display_string);
      }

      if(display_option == current_option)
      {
#ifdef NSPIRE_BUILD
        current_option_y = (line * 10) + 40;
#endif
        print_string_pad(line_buffer, COLOR_ACTIVE_ITEM, COLOR_BG, 10,
         (line * 10) + 40, 41);
      }
      else
      {
        print_string_pad(line_buffer, COLOR_INACTIVE_ITEM, COLOR_BG, 10,
         (line * 10) + 40, 41);
      }
    }

#ifdef NSPIRE_BUILD
    /* What the last action did, until the next key. */
    if(menu_status[0])
    {
      clear_help();
      print_string_pad(menu_status, COLOR_ACTIVE_ITEM, COLOR_BG, 8, 210, 70);
    }
    else
#endif
    print_help_wrapped(current_option->help_string);

    flip_screen();
#ifdef NSPIRE_BUILD
	update_at_vblank();
#endif

    gui_action = get_gui_input();
#ifdef NSPIRE_BUILD
    if(gui_action != CURSOR_NONE)
      menu_status[0] = 0;
    /* esc goes back, like the menu key. */
    if(gui_action == CURSOR_BACK)
      gui_action = CURSOR_EXIT;
#endif

    switch(gui_action)
    {
      case CURSOR_DOWN:
        current_option_num = (current_option_num + 1) %
          current_menu->num_options;

        current_option = current_menu->options + current_option_num;
        clear_help();
        break;

      case CURSOR_UP:
        if(current_option_num)
          current_option_num--;
        else
          current_option_num = current_menu->num_options - 1;

        current_option = current_menu->options + current_option_num;
        clear_help();
        break;

      case CURSOR_RIGHT:
      case CURSOR_LEFT:
#ifdef NSPIRE_BUILD
        if(current_option->option_type & (KEY_BINDING_OPTION | COMBO_SLOT_OPTION))
        {
          key_column ^= 1;
          break;
        }
#endif
        if(current_option->option_type & (NUMBER_SELECTION_OPTION |
         STRING_SELECTION_OPTION))
        {
          u32 count = current_option->num_options;
          u32 step = 1;
#ifdef NSPIRE_BUILD
          /* Long lists (the save slots) move 10 at a time while held. */
          if(count >= 100 && gui_repeat_count() > 20)
            step = 10;
#endif
          if(gui_action == CURSOR_RIGHT)
            *(current_option->current_option) =
             (*current_option->current_option + step) % count;
          else
            *(current_option->current_option) =
             (*current_option->current_option + count - step % count) % count;

          if(current_option->passive_function)
            current_option->passive_function();
        }
        break;

      case CURSOR_EXIT:
        if(current_menu == &main_menu)
          menu_exit();

        choose_menu(&main_menu);
        break;

      case CURSOR_SELECT:
#ifdef NSPIRE_BUILD
        if(current_option->option_type & KEY_BINDING_OPTION)
        {
          key_binding b;
          print_string_pad("press a key (an arrow cancels)", COLOR_ACTIVE_ITEM,
           COLOR_BG, 10 + 18 * FONT_WIDTH, current_option_y, 30);
          flip_screen();
          b = key_capture();
          if(b.key)
            key_bindings[current_option->num_options][key_column] = b;
          break;
        }
        if(current_option->option_type & SWIPE_MULTI_OPTION)
        {
          u32 dir = current_option->num_options;
          gui_edit_action_mask(swipe_dir_names[dir], &swipe_action_masks[dir]);
          break;
        }
        if(current_option->option_type & COMBO_SLOT_OPTION)
        {
          u32 slot = current_option->num_options;
          if(key_column == 0)
          {
            key_binding b;
            print_string_pad("press a key or swipe (arrow cancels)", COLOR_ACTIVE_ITEM,
             COLOR_BG, 10 + 18 * FONT_WIDTH, current_option_y, 36);
            flip_screen();
            b = key_capture();
            if(b.key)
              combo_bindings[slot].trigger = b;
          }
          else
          {
            char title[24];
            snprintf(title, sizeof(title), "Combo %u buttons", (unsigned)(slot + 1));
            gui_edit_action_mask(title, &combo_bindings[slot].action_mask);
          }
          break;
        }
#endif
        if(current_option->option_type & ACTION_OPTION)
          current_option->action_function();

        if(current_option->option_type & SUBMENU_OPTION)
          choose_menu(current_option->sub_menu);
        break;

#ifdef NSPIRE_BUILD
      case CURSOR_CLEAR:
        if(current_option->option_type & KEY_BINDING_OPTION)
        {
          key_bindings[current_option->num_options][key_column].key = 0;
          key_bindings[current_option->num_options][key_column].with = 0;
        }
        else if(current_option->option_type & SWIPE_MULTI_OPTION)
          swipe_action_masks[current_option->num_options] = 0;
        else if(current_option->option_type & COMBO_SLOT_OPTION)
        {
          u32 slot = current_option->num_options;
          if(key_column == 0)
          {
            combo_bindings[slot].trigger.key = 0;
            combo_bindings[slot].trigger.with = 0;
          }
          else
            combo_bindings[slot].action_mask = 0;
        }
        break;
#endif

      default:
        break;
    }
  }

  set_gba_resolution(screen_scale);
  video_resolution_small();
  menu_get_clock_speed();
  set_clock_speed();
#if defined(NSPIRE_LIBRETRO)
  save_config_file();
#endif
#ifndef NSPIRE_BUILD
  SDL_PauseAudio(0);
#endif

  return return_value;
}
