/* Cartridge save (SRAM / Flash / EEPROM) persistence for NSPIRE_LIBRETRO. */

#include "common.h"
#include "gba_memory.h"
#include "streams/file_stream.h"

#include <string.h>

char backup_filename[512];

#define WRITE_BACKUP_DELAY 10

static u32 backup_update_count = WRITE_BACKUP_DELAY + 1;

void nspire_backup_mark_dirty(void)
{
  backup_update_count = WRITE_BACKUP_DELAY;
}

void nspire_load_cartridge_backup(void)
{
  if (!gamepak_filename[0])
    return;
  change_ext((u8 *)gamepak_filename, (u8 *)backup_filename, (u8 *)".sav.tns");
  load_backup(backup_filename);
  backup_update_count = WRITE_BACKUP_DELAY + 1;
}

static u32 nspire_backup_file_size(void)
{
  switch (backup_type)
  {
    case BACKUP_SRAM:
      return 0x8000;

    case BACKUP_FLASH:
      if (flash_bank_cnt == FLASH_SIZE_128KB)
        return 0x20000;
      return 0x10000;

    case BACKUP_EEPROM:
      if (eeprom_size == EEPROM_512_BYTE)
        return 0x200;
      return 0x2000;

    default:
      break;
  }
  return 0;
}

u32 load_backup(char *name)
{
  RFILE *fd;
  int64_t sz64;
  u32 sz;

  fd = filestream_open(name, RETRO_VFS_FILE_ACCESS_READ,
                       RETRO_VFS_FILE_ACCESS_HINT_NONE);
  if (!fd)
  {
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }

  sz64 = filestream_get_size(fd);
  if (sz64 <= 0 || sz64 > (int64_t)sizeof(gamepak_backup))
  {
    filestream_close(fd);
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }

  sz = (u32)sz64;
  if (filestream_read(fd, gamepak_backup, sz) != (int64_t)sz)
  {
    filestream_close(fd);
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
    return 0;
  }
  filestream_close(fd);

  switch (sz)
  {
    case 0x200:
      backup_type = BACKUP_EEPROM;
      eeprom_size = EEPROM_512_BYTE;
      break;

    case 0x2000:
      backup_type = BACKUP_EEPROM;
      eeprom_size = EEPROM_8_KBYTE;
      break;

    case 0x8000:
      backup_type = BACKUP_SRAM;
      break;

    case 0x10000:
      backup_type = BACKUP_FLASH;
      flash_bank_cnt = FLASH_SIZE_64KB;
      flash_device_id = FLASH_DEVICE_MACRONIX_64KB;
      break;

    case 0x20000:
      backup_type = BACKUP_FLASH;
      flash_bank_cnt = FLASH_SIZE_128KB;
      flash_device_id = FLASH_DEVICE_MACRONIX_128KB;
      break;

    default:
      memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
      return 0;
  }

  backup_type_reset = backup_type;
  flash_mode = FLASH_BASE_MODE;
  flash_command_position = 0;
  flash_bank_num = 0;
  eeprom_mode = EEPROM_BASE_MODE;
  eeprom_address = 0;
  eeprom_counter = 0;
  return 1;
}

u32 save_backup(char *name)
{
  RFILE *fd;
  u32 backup_size;
  int64_t w;

  if (backup_type != BACKUP_SRAM && backup_type != BACKUP_FLASH &&
      backup_type != BACKUP_EEPROM)
    return 0;

  backup_size = nspire_backup_file_size();
  if (!backup_size)
    return 0;

  fd = filestream_open(name, RETRO_VFS_FILE_ACCESS_WRITE,
                       RETRO_VFS_FILE_ACCESS_HINT_NONE);
  if (!fd)
    return 0;

  w = filestream_write(fd, gamepak_backup, (int64_t)backup_size);
  filestream_close(fd);
  return (w == (int64_t)backup_size) ? 1 : 0;
}

void update_backup(void)
{
  if (!update_backup_flag || !gamepak_filename[0])
    return;

  if (backup_update_count != WRITE_BACKUP_DELAY + 1)
    backup_update_count--;

  if (backup_update_count == 0)
  {
    change_ext((u8 *)gamepak_filename, (u8 *)backup_filename, (u8 *)".sav.tns");
    save_backup(backup_filename);
    backup_update_count = WRITE_BACKUP_DELAY + 1;
  }
}

void update_backup_force(void)
{
  if (!gamepak_filename[0])
    return;
  change_ext((u8 *)gamepak_filename, (u8 *)backup_filename, (u8 *)".sav.tns");
  save_backup(backup_filename);
  backup_update_count = WRITE_BACKUP_DELAY + 1;
}
