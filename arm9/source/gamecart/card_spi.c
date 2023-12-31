/*
 *  This file is based on SPI.cpp from TWLSaveTool. Its copyright notice is
 *  reproduced below.
 *
 *  Copyright (C) 2015-2016 TuxSH
 *
 *  TWLSaveTool is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <arm.h>
#include "card_spi.h"
#include <spi.h>
#include "timer.h"

#define SPI_CMD_RDSR 5
#define SPI_CMD_WREN 6

#define SPI_512B_EEPROM_CMD_WRLO 2
#define SPI_512B_EEPROM_CMD_WRHI 10
#define SPI_512B_EEPROM_CMD_RDLO 3
#define SPI_512B_EEPROM_CMD_RDHI 11

#define SPI_EEPROM_CMD_WRITE 2

#define SPI_CMD_READ 3

#define SPI_CMD_PP 2
#define SPI_FLASH_CMD_PW 10
#define SPI_FLASH_CMD_RDID 0x9f
#define SPI_FLASH_CMD_SE 0xd8
#define SPI_FLASH_CMD_PE 0xdb
#define SPI_FLASH_CMD_MXIC_SE 0x20

#define SPI_FLG_WIP 1
#define SPI_FLG_WEL 2

// declarations for actual implementations
int CardSPIEnableWriting_512B(CardSPIType type);
int CardSPIEnableWriting_regular(CardSPIType type);
int CardSPIReadSaveData_9bit(CardSPIType type, u32 offset, void* data, u32 size);
int CardSPIReadSaveData_16bit(CardSPIType type, u32 offset, void* data, u32 size);
int CardSPIReadSaveData_24bit(CardSPIType type, u32 offset, void* data, u32 size);
int CardSPIWriteSaveData_9bit(CardSPIType type, u32 offset, const void* data, u32 size);
int CardSPIWriteSaveData_16bit(CardSPIType type, u32 offset, const void* data, u32 size);
int CardSPIWriteSaveData_24bit_write(CardSPIType type, u32 offset, const void* data, u32 size);
int CardSPIWriteSaveData_24bit_erase_program(CardSPIType type, u32 offset, const void* data, u32 size);
int CardSPIEraseSector_emulated(CardSPIType type, u32 offset);
int CardSPIEraseSector_real(CardSPIType type, u32 offset);

const CardSPITypeData EEPROM_512B = { CardSPIEnableWriting_512B, CardSPIReadSaveData_9bit, CardSPIWriteSaveData_9bit, CardSPIEraseSector_emulated, 0xffff, 1 << 9, 16, 16, 16, 0, 0, 0 };

const CardSPITypeData EEPROM_DUMMY = { CardSPIEnableWriting_regular, CardSPIReadSaveData_16bit, CardSPIWriteSaveData_16bit, CardSPIEraseSector_emulated, 0xffff, UINT32_MAX, 1, 1, 1, SPI_EEPROM_CMD_WRITE, 0, 0 };
const CardSPITypeData EEPROMTypes[] = {
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_16bit, CardSPIWriteSaveData_16bit, CardSPIEraseSector_emulated, 0xffff, 1 << 13, 32, 32, 32, SPI_EEPROM_CMD_WRITE, 0, 0}, // EEPROM 8 KB
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_16bit, CardSPIWriteSaveData_16bit, CardSPIEraseSector_emulated, 0xffff, 1 << 16, 128, 128, 128, SPI_EEPROM_CMD_WRITE, 0, 0}, // EEPROM 64 KB
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_write, CardSPIEraseSector_emulated, 0xffff, 1 << 17, 256, 256, 256, SPI_EEPROM_CMD_WRITE, 0, 0}, // EEPROM 128 KB
};

const CardSPITypeData FLASH_DUMMY = { NULL, CardSPIReadSaveData_24bit, NULL, NULL, 0x0, 0, 0, 0, 0, 0, 0, 0 };
const CardSPITypeData flashTypes[] = {
    // NTR/TWL

    // Most common flash chip in DS games, in 3 different sizes (256kB: ST M45PE20, 512kB: ST M45PE40, 1MB: ST M45PE80)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_write, CardSPIEraseSector_real, 0x2040, 0, 65536, 256, 256, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_SE },

    // Found in Mario Kart, for example (always 256kB, Sanyo LE25FW203T)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_erase_program, CardSPIEraseSector_real, 0x6216, 1 << 18, 65536, 256, 65536, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_SE },

    // Found in some copies of Pokemon Diamond/Pearl (always 512kB, Sanyo ??)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_write, CardSPIEraseSector_real, 0x6211, 1 << 19, 65536, 256, 256, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_SE },

    // Found in Art Academy, and maybe some other ones (8MB: ST M25P64)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_erase_program, CardSPIEraseSector_real, 0x2020, 0, 65536, 256, 65536, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_SE },

    // Found in some DS bootlegs (ST M25PE40VP in my case)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_erase_program, CardSPIEraseSector_real, 0x2080, 0, 65536, 256, 65536, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_SE },

    // This is the most common type of flash 3DS cartridges. Not normally found in DS ones, but check anyway. (Custom MXIC chips)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_erase_program, CardSPIEraseSector_real, 0xC222, 0, 4096, 32, 4096, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_MXIC_SE },

    // Found this in some copies of ArtAcademy, I think it's something from MXIC (Thank you for the help @FerozElMejor on Discord)
    { CardSPIEnableWriting_regular, CardSPIReadSaveData_24bit, CardSPIWriteSaveData_24bit_erase_program, CardSPIEraseSector_real, 0xC220, 0, 4096, 32, 4096, SPI_FLASH_CMD_PW, SPI_CMD_PP, SPI_FLASH_CMD_MXIC_SE },
};

const CardSPITypeData * const FLASH_CTR_GENERIC = flashTypes + 5;

#define REG_CFG9_CARDCTL      *((vu16*)0x1000000C)
#define CARDCTL_SPICARD       (1u<<8)

int CardSPIWriteRead(bool infrared, const void* cmd, u32 cmdSize, void* answer, u32 answerSize, const void* data, u32 dataSize) {
    u32 headerFooterVal = 0;

    REG_CFG9_CARDCTL |= CARDCTL_SPICARD;

    if (infrared) {
        SPI_XferInfo irXfer = { &headerFooterVal, 1, false };
        SPI_DoXfer(SPI_DEV_CART_IR, &irXfer, 1, false);
        // Wait as specified by GBATEK (0x800 cycles at 33 MHz)
        ARM_WaitCycles(0x800 * 4);
    }

    SPI_XferInfo transfers[3] = {
        { (u8*) cmd, cmdSize, false },
        { answer, answerSize, true },
        { (u8*) data, dataSize, false },
    };
    SPI_DoXfer(SPI_DEV_CART_FLASH, transfers, 3, true);

    REG_CFG9_CARDCTL &= ~CARDCTL_SPICARD;

    if (infrared) {
        // Wait as specified by GBATEK (0x800 cycles at 33 MHz)
        ARM_WaitCycles(0x800 * 4);
    }

    return 0;
}

int CardSPIWaitWriteEnd(bool infrared, u32 timeout) {
    u8 cmd = SPI_CMD_RDSR, statusReg = 0;
    int res = 0;
    u64 time_start = timer_start();

    do {
        res = CardSPIWriteRead(infrared, &cmd, 1, &statusReg, 1, 0, 0);
        if (res) return res;
        if (timer_msec(time_start) > timeout) return 1;
    } while(statusReg & SPI_FLG_WIP);

    return 0;
}

int CardSPIEnableWriting_512B(CardSPIType type) {
    u8 cmd = SPI_CMD_WREN;
    return CardSPIWriteRead(type.infrared, &cmd, 1, NULL, 0, 0, 0);
}

int CardSPIEnableWriting_regular(CardSPIType type) {
    u8 cmd = SPI_CMD_WREN, statusReg = 0;
    int res = CardSPIWriteRead(type.infrared, &cmd, 1, NULL, 0, 0, 0);

    if (res) return res;
    cmd = SPI_CMD_RDSR;

    do {
        res = CardSPIWriteRead(type.infrared, &cmd, 1, &statusReg, 1, 0, 0);
        if (res) return res;
    } while(statusReg & ~SPI_FLG_WEL);

    return 0;
}

int CardSPIEnableWriting(CardSPIType type) {
    if (type.chip == NO_CHIP) return 1;
    return type.chip->enableWriting(type);
}

int _SPIWriteTransaction(CardSPIType type, void* cmd, u32 cmdSize, const void* data, u32 dataSize) {
    int res;
    if ((res = CardSPIEnableWriting(type))) return res;
    if ((res = CardSPIWriteRead(type.infrared, cmd, cmdSize, NULL, 0, (void*) ((u8*) data), dataSize))) return res;
    return CardSPIWaitWriteEnd(type.infrared, 10000);
}

int CardSPIReadJEDECIDAndStatusReg(bool infrared, u32* id, u8* statusReg) {
    u8 cmd = SPI_FLASH_CMD_RDID;
    u8 reg = 0;
    u8 idbuf[3] = { 0 };
    u32 id_ = 0;
    int res = CardSPIWaitWriteEnd(infrared, 10);
    if (res) return res;

    if ((res = CardSPIWriteRead(infrared, &cmd, 1, idbuf, 3, 0, 0))) return res;

    id_ = (idbuf[0] << 16) | (idbuf[1] << 8) | idbuf[2];
    cmd = SPI_CMD_RDSR;

    if ((res = CardSPIWriteRead(infrared, &cmd, 1, &reg, 1, 0, 0))) return res;

    if (id) *id = id_;
    if (statusReg) *statusReg = reg;

    return 0;
}

u32 CardSPIGetPageSize(CardSPIType type) {
    if (type.chip == NO_CHIP) return 0;
    return type.chip->pageSize;
}

u32 CardSPIGetEraseSize(CardSPIType type) {
    if (type.chip == NO_CHIP) return 0;
    return type.chip->eraseSize;
}

u32 CardSPIGetCapacity(CardSPIType type) {
    if (type.chip == NO_CHIP) return 0;
    if (type.chip->capacity != 0) {
        return type.chip->capacity;
    } else {
        u32 jedecid;
        CardSPIReadJEDECIDAndStatusReg(type.infrared, &jedecid, NULL);
        u8 size_exponent = jedecid & 0xff;
        if ((size_exponent >= 8) && (size_exponent <= 24)) {
            // Thanks to @wwylele for pointing this out
            return 1 << size_exponent;
        } else {
            return 0;
        }
    }
}

int CardSPIWriteSaveData_9bit(CardSPIType type, u32 offset, const void* data, u32 size) {
    u8 cmd[2] = { (offset >= 0x100) ? SPI_512B_EEPROM_CMD_WRHI : SPI_512B_EEPROM_CMD_WRLO, (u8) offset };

    return _SPIWriteTransaction(type, cmd, 2, (void*) ((u8*) data), size);
}

int CardSPIWriteSaveData_16bit(CardSPIType type, u32 offset, const void* data, u32 size) {
    u8 cmd[3] = { type.chip->writeCommand, (u8)(offset >> 8), (u8) offset };

    return _SPIWriteTransaction(type, cmd, 3, (void*) ((u8*) data), size);
}

int CardSPIWriteSaveData_24bit_write(CardSPIType type, u32 offset, const void* data, u32 size) {
    u8 cmd[4] = { type.chip->writeCommand, (u8)(offset >> 16), (u8)(offset >> 8), (u8) offset };

    return _SPIWriteTransaction(type, cmd, 4, (void*) ((u8*) data), size);
}

int CardSPIWriteSaveData_24bit_erase_program(CardSPIType type, u32 offset, const void* data, u32 size) {
    u8 cmd[4] = { type.chip->programCommand };
    const u32 pageSize = CardSPIGetPageSize(type);
    const u32 eraseSize = CardSPIGetEraseSize(type);
    int res;

    u8 *newData = NULL;
    if (offset % eraseSize || size < eraseSize) {
        u32 sectorStart = (offset / eraseSize) * eraseSize;
        newData = malloc(eraseSize);
        if (!newData) return 1;
        if ((res = CardSPIReadSaveData(type, sectorStart, newData, eraseSize))) {
            free(newData);
            return res;
        }
        memcpy(newData + (offset % eraseSize), data, size);
        data = newData;
        offset = sectorStart;
    }

    if ((res = CardSPIEraseSector(type, offset))) {
        free(newData);
        return res;
    }

    for(u32 pos = offset; pos < offset + eraseSize; pos += pageSize) {
        cmd[1] = (u8)(pos >> 16);
        cmd[2] = (u8)(pos >> 8);
        cmd[3] = (u8) pos;
        for(int i = 0; i < 10; i++) {
            if (!(res = _SPIWriteTransaction(type, cmd, 4, (void*) ((u8*) data - offset + pos), pageSize))) {
                break;
            }
            CardSPIWriteRead(type.infrared, "\x04", 1, NULL, 0, NULL, 0);
        }
        if(res) {
            free(newData);
            return res;
        }
    }

    free(newData);
    return 0;
}

int CardSPIWriteSaveData(CardSPIType type, u32 offset, const void* data, u32 size) {
    if (type.chip == NO_CHIP) return 1;

    if (size == 0) return 0;
    u32 end = offset + size;
    u32 pos = offset;
    u32 writeSize = type.chip->writeSize;
    if (writeSize == 0) return 0xC8E13404;

    int res = CardSPIWaitWriteEnd(type.infrared, 1000);
    if (res) return res;

    while(pos < end) {
        u32 remaining = end - pos;
        u32 nb = writeSize - (pos % writeSize);

        u32 dataSize = (remaining < nb) ? remaining : nb;

        if ((res = type.chip->writeSaveData(type, pos, (void*) ((u8*) data - offset + pos), dataSize))) return res;

        pos = ((pos / writeSize) + 1) * writeSize; // truncate
    }

    return 0;
}

int CardSPIReadSaveData_9bit(CardSPIType type, u32 pos, void* data, u32 size) {
    u8 cmd[4];
    u32 cmdSize = 2;

    u32 end = pos + size;

    u32 read = 0;
    if (pos < 0x100) {
        u32 len = min(0x100 - pos, size);
        cmd[0] = SPI_512B_EEPROM_CMD_RDLO;
        cmd[1] = (u8) pos;

        int res = CardSPIWriteRead(type.infrared, cmd, cmdSize, data, len, NULL, 0);
        if (res) return res;

        read += len;
    }

    if (end >= 0x100) {
        u32 len = min(end - 0x100, size);

        cmd[0] = SPI_512B_EEPROM_CMD_RDHI;
        cmd[1] = (u8)(pos + read);

        int res = CardSPIWriteRead(type.infrared, cmd, cmdSize, (void*)((u8*)data + read), len, NULL, 0);

        if (res) return res;
    }

    return 0;
}

int CardSPIReadSaveData_16bit(CardSPIType type, u32 offset, void* data, u32 size) {
    u8 cmd[3] = { SPI_CMD_READ, (u8)(offset >> 8), (u8) offset };

    return CardSPIWriteRead(type.infrared, cmd, 3, data, size, NULL, 0);
}

int CardSPIReadSaveData_24bit(CardSPIType type, u32 offset, void* data, u32 size) {
    u8 cmd[4] = { SPI_CMD_READ, (u8)(offset >> 16), (u8)(offset >> 8), (u8) offset };

    return CardSPIWriteRead(type.infrared, cmd, 4, data, size, NULL, 0);
}

int CardSPIReadSaveData(CardSPIType type, u32 offset, void* data, u32 size) {
    if (type.chip == NO_CHIP) return 1;

    if (size == 0) return 0;

    int res = CardSPIWaitWriteEnd(type.infrared, 1000);
    if (res) return res;

    return type.chip->readSaveData(type, offset, data, size);
}

int CardSPIEraseSector_emulated(CardSPIType type, u32 offset) {
    u32 blockSize = CardSPIGetEraseSize(type);
    u8 *fill_buf = malloc(CardSPIGetEraseSize(type));
    if (!fill_buf) return 1;
    memset(fill_buf, 0xff, blockSize);
    offset = (offset / blockSize) * blockSize;

    int res = CardSPIWriteSaveData(type, offset, fill_buf, blockSize);
    free(fill_buf);
    return res;
}

int CardSPIEraseSector_real(CardSPIType type, u32 offset) {
    u8 cmd[4] = { type.chip->eraseCommand, (u8)(offset >> 16), (u8)(offset >> 8), (u8) offset };

    int res = CardSPIWaitWriteEnd(type.infrared, 10000);
    if (res) return res;

    return _SPIWriteTransaction(type, cmd, 4, NULL, 0);
}


int CardSPIEraseSector(CardSPIType type, u32 offset) {
    if (type.chip == NO_CHIP) return 1;
    return type.chip->eraseSector(type, offset);
}

int CardSPIErase(CardSPIType type) {
    for (u32 pos = 0; pos < CardSPIGetCapacity(type); pos += CardSPIGetEraseSize(type)) {
        int res = CardSPIEraseSector(type, pos);
        if(res) return res;
    }
    return 0;
}

// The following routine use code from savegame-manager:

/*
 * savegame_manager: a tool to backup and restore savegames from Nintendo
 *  DS cartridges. Nintendo DS and all derivative names are trademarks
 *  by Nintendo. EZFlash 3-in-1 is a trademark by EZFlash.
 *
 * auxspi.cpp: A thin reimplementation of the AUXSPI protocol
 *   (high level functions)
 *
 * Copyright (C) Pokedoc (2010)
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


int _SPIIsDataMirrored(CardSPIType type, int size, bool* mirrored) {
    u32 offset0 = (size-1);        //      n KB
    u32 offset1 = (2*size-1);      //     2n KB

    u8 buf1;     //      +0k data        read -> write
    u8 buf2;     //      +n k data        read -> read
    u8 buf3;     //      +0k ~data          write
    u8 buf4;     //      +n k data new    comp buf2

    int res;

    if ((res = CardSPIReadSaveData(type, offset0, &buf1, 1))) return res;
    if ((res = CardSPIReadSaveData(type, offset1, &buf2, 1))) return res;
    buf3=~buf1;
    if ((res = CardSPIWriteSaveData(type, offset0, &buf3, 1))) return res;
    if ((res = CardSPIReadSaveData(type, offset1, &buf4, 1))) return res;
    if ((res = CardSPIWriteSaveData(type, offset0, &buf1, 1))) return res;

    *mirrored = buf2 != buf4;
    return 0;
}

CardSPIType CardSPIGetCardSPIType(bool infrared) {
    u8 sr = 0;
    u32 jedec = 0;
    CardSPIType t = {NO_CHIP, infrared};
    int res;

    res = CardSPIReadJEDECIDAndStatusReg(infrared, &jedec, &sr);
    if (res) return (CardSPIType) {NO_CHIP, false};

    if ((sr & 0xfd) == 0x00 && (jedec != 0x00ffffff)) { t.chip = &FLASH_DUMMY; }
    if ((sr & 0xfd) == 0xF0 && (jedec == 0x00ffffff)) { return (CardSPIType) { &EEPROM_512B, false }; }
    if ((sr & 0xfd) == 0x00 && (jedec == 0x00ffffff)) { t = (CardSPIType) { &EEPROM_DUMMY, false }; }

    if(t.chip == NO_CHIP) {
        return (CardSPIType) {NO_CHIP, false};
    }

    if (t.chip == &EEPROM_DUMMY) {
        bool mirrored = false;
        size_t i;

        for(i = 0; i < sizeof(EEPROMTypes) / sizeof(CardSPITypeData) - 1; i++) {
            if ((res = _SPIIsDataMirrored(t, CardSPIGetCapacity((CardSPIType) {EEPROMTypes + i, false}), &mirrored))) return (CardSPIType) {NO_CHIP, false};
            if (mirrored) {
                return (CardSPIType) {EEPROMTypes + i, false};
            }
        }
        return (CardSPIType) { EEPROMTypes + i, false };
    }

    for(size_t i = 0; i < sizeof(flashTypes) / sizeof(CardSPITypeData); i++) {
        if (flashTypes[i].jedecId == (jedec >> 8)) {
            return (CardSPIType) { flashTypes + i, infrared };
        }
    }

    return (CardSPIType) { NO_CHIP, infrared };

}
