#ifndef AY8910_H
#define AY8910_H

#include <cstdint>
#include "deftypes.h"   // MemoryReadByte/WriteByte + z80Port structs (was aae_mame_driver.h)

constexpr int MAX_8910 = 5;   // Gyruss uses 5 AY-3-8910s

using AY8910PortRead  = uint8_t (*)(void);
using AY8910PortWrite = void    (*)(uint8_t value);

struct AY8910Config {
    int num_chips;                          // 1..MAX_8910
    int base_clock;                         // master clock Hz
    int mixing_level[MAX_8910];             // 0..255, fed to sample_set_volume_mixer
    AY8910PortRead  port_a_read [MAX_8910]; // nullable
    AY8910PortRead  port_b_read [MAX_8910]; // nullable
    AY8910PortWrite port_a_write[MAX_8910]; // nullable
    AY8910PortWrite port_b_write[MAX_8910]; // nullable
};

// Bank lifecycle
int  ay8910_sh_start (const AY8910Config* cfg);
void ay8910_sh_stop  (void);
void ay8910_sh_update(void);
void ay8910_reset    (int chip);    // -1 = all chips

// Direct register access
void    ay8910_write(int chip, int addr, uint8_t data);
uint8_t ay8910_read (int chip);

// MEM_ADDR trampolines (chip 0..4)
void    ay8910_0_control_w(uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
void    ay8910_0_data_w   (uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
uint8_t ay8910_0_data_r   (uint32_t addr,               struct MemoryReadByte*  mrb);
void    ay8910_1_control_w(uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
void    ay8910_1_data_w   (uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
uint8_t ay8910_1_data_r   (uint32_t addr,               struct MemoryReadByte*  mrb);
void    ay8910_2_control_w(uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
void    ay8910_2_data_w   (uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
uint8_t ay8910_2_data_r   (uint32_t addr,               struct MemoryReadByte*  mrb);
void    ay8910_3_control_w(uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
void    ay8910_3_data_w   (uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
uint8_t ay8910_3_data_r   (uint32_t addr,               struct MemoryReadByte*  mrb);
void    ay8910_4_control_w(uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
void    ay8910_4_data_w   (uint32_t addr, uint8_t data, struct MemoryWriteByte* mwb);
uint8_t ay8910_4_data_r   (uint32_t addr,               struct MemoryReadByte*  mrb);

// PORT_ADDR (Z80 IO) trampolines
uint16_t ay8910_0_data_port_r   (uint16_t off,                struct z80PortRead*  zpr);
void     ay8910_0_control_port_w(uint16_t off, uint8_t value, struct z80PortWrite* zpw);
void     ay8910_0_data_port_w   (uint16_t off, uint8_t value, struct z80PortWrite* zpw);
uint16_t ay8910_1_data_port_r   (uint16_t off,                struct z80PortRead*  zpr);
void     ay8910_1_control_port_w(uint16_t off, uint8_t value, struct z80PortWrite* zpw);
void     ay8910_1_data_port_w   (uint16_t off, uint8_t value, struct z80PortWrite* zpw);
uint16_t ay8910_2_data_port_r   (uint16_t off,                struct z80PortRead*  zpr);
void     ay8910_2_control_port_w(uint16_t off, uint8_t value, struct z80PortWrite* zpw);
void     ay8910_2_data_port_w   (uint16_t off, uint8_t value, struct z80PortWrite* zpw);
uint16_t ay8910_3_data_port_r   (uint16_t off,                struct z80PortRead*  zpr);
void     ay8910_3_control_port_w(uint16_t off, uint8_t value, struct z80PortWrite* zpw);
void     ay8910_3_data_port_w   (uint16_t off, uint8_t value, struct z80PortWrite* zpw);
uint16_t ay8910_4_data_port_r   (uint16_t off,                struct z80PortRead*  zpr);
void     ay8910_4_control_port_w(uint16_t off, uint8_t value, struct z80PortWrite* zpw);
void     ay8910_4_data_port_w   (uint16_t off, uint8_t value, struct z80PortWrite* zpw);

#endif
