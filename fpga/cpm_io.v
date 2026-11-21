`timescale 1ns / 1ps

// Z80 I/O 
// z80pack compatible I/O 
// Copyright (C) 2019  Skip Hansen

//  This program is free software; you can redistribute it and/or modify it
//  under the terms and conditions of the GNU General Public License,
//  version 2, as published by the Free Software Foundation.
//
//  This program is distributed in the hope it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

// When the Z80 reads an I/O port the io_port_adr register is updated,
// the io_port_status register is set to IO_STAT_READ and then the z80 
// is held in wait until the riscv writes the requested data to the 
// in_port_data data register.  

// When the Z80 writes an I/O port the io_port_adr and out_port_data 
// registers are updated, the io_port_status register is set to IO_STAT_WRITE
// and then the z80 is held in wait until the riscv reads the data.
// 
// RISC V  Z80
// Adr     Adr  Usage                        Read     Write    Notes
// 0x00 (0)       0  - Console status         Z80     RISC V    
//        --      1  - Console in             Z80     ---       1 
//        --      1  - Console out            ---     Z80       2 
// 0x04 (1)       10 - Drive                  Both    Z80       
// 0x08 (2)       11 - Track                  Both    Z80       
// 0x0c (3)       12 - Sector low             Both    Z80       
// --             13 - Disk command           ---     Z80       2 
// --             13 - Disk command           Z80     ---       
// ---            14 - Disk status            Z80     RISC V    4 
// 0x14 (5)       15 - DMA Adr LSB            Both    Z80       
// 0x18 (6)       16 - DMA Adr MSB            Both    Z80       
// 0x1c (7)       17 - Sector high            Both    Z80       
// --             xx - Other                  Z80               
// 0x20 (8)       -- - Z80 I/O Adr            RISC V  ---       
// 0x24 (9)       -- - Z80 Out Data           RISC V  ---       
// 0x28 (10)      -- - Z80 In Data            ---     RISC V    
// 0x2c (11)      -- - Z80 I/O Status         RISC V  ---       3 
// 0x30 (12)      -- - Font foreground color  RISC V  RISC V    
// 0x34 (13)      -- - Font background color  RISC V  RISC V    
// 0x38 (14)      -- - Sound control          RISC V  RISC V    5
// 0x3c (15)      -- - Z80 speed              RISC V  RISC V    6
// --             32h- Beeper (ITT 3030)      ---     Z80       7
// Notes:
//  1 - Z80 held in wait until RISC V write the data to complete the Z80 I/O 
//      to the "Z80 In Data" register.
//  2 - Z80 held in wait until RISC V reads data for I/O write from the 
//      Z80 In Data register
//  3 - FSM state, written by hardware
//  4 - Handled by generic I/O hander
//  5 - bit 0: sound on, bit 1: bell (the firmware's beep, for BEL)
//  6 - Z80 clock in MHz, 0 = full speed (25 MHz); see z80_cen below
//  7 - bit 0 switches the beeper on and off, as on the ITT 3030.  Handled
//      here rather than by the RISC V so that a program timing its tones
//      with loops gets them at the time it expects.

module cpm_io(
    input wire clk,
    input wire reset,

// Z80 interface
    input wire z80_iord,
    input wire z80_iowr,
    input wire [7:0] z80adr,
    output reg [7:0] z80di,
    input wire [7:0] z80do,
    output reg z80_io_ready,
    input wire z80hlt,
    input wire z80_reset,

// RISC V interface
    input wire io_valid,
    input wire [23:0] rv_wdata,
    input wire [3:0] rv_adr,
    input wire rv_wstr,
    output reg [23:0] rv_rdata,
// vga_mixer interface
    output reg [23:0] font_fg_color,
    output reg [23:0] font_bg_color,
// sound and speed
    output wire beep,             // the beeper should sound
    output reg z80_cen            // Z80 clock enable (clk is the Z80's clock)
    );

    reg [4:0] z80_mhz;

    reg beeper;                   // port 32h bit 0
    reg [1:0] sound_ctrl;         // bit 0: sound on, bit 1: bell
    assign beep = sound_ctrl[0] && (beeper || sound_ctrl[1]);

    reg [7:0] disk_drive;
    reg [7:0] disk_track;
    reg [7:0] disk_sector_lsb;
    reg [7:0] disk_sector_msb;
    reg [7:0] disk_dma_adr_lsb;
    reg [7:0] disk_dma_adr_msb;
    reg [7:0] io_port_adr;
    reg [7:0] out_port_data;
    reg [1:0] io_port_status;
    reg [7:0] console_status;

    localparam IO_STAT_IDLE  = 2'd0;
    localparam IO_STAT_WRITE = 2'd1;
    localparam IO_STAT_READ  = 2'd2;
    localparam IO_STAT_READY = 2'd3;
    
    localparam BLACK = 24'd0;
    localparam GREEN = 24'h00ff00;
        
    always@(posedge clk) begin
        if (reset) begin
            io_port_status <= IO_STAT_IDLE;
            console_status <= 8'd0;
            disk_drive <= 8'd0;
            disk_track <= 8'd0;
            disk_sector_lsb <= 8'd0;
            disk_dma_adr_lsb <= 8'd0;
            disk_dma_adr_msb <= 8'd0;
            disk_sector_msb <= 8'd0;
            io_port_adr <= 8'd0;
            z80di <= 8'd0;
            font_fg_color <= GREEN;
            font_bg_color <= BLACK;
            beeper <= 1'b0;
            sound_ctrl <= 2'd0;
            z80_mhz <= 5'd0;
        end
        else begin
            if (io_valid) begin
                 if (rv_wstr != 0) begin
                    case (rv_adr)
                        4'd0: console_status <= rv_wdata[7:0];
                        4'd1: disk_drive <= rv_wdata[7:0];
                        4'd2: disk_track <= rv_wdata[7:0];
                        4'd3: disk_sector_lsb <= rv_wdata[7:0];
                        4'd5: disk_dma_adr_lsb <= rv_wdata[7:0];
                        4'd6: disk_dma_adr_msb <= rv_wdata[7:0];
                        4'd7: disk_sector_msb <= rv_wdata[7:0];
                        4'd8: io_port_adr <= rv_wdata[7:0];
                        4'd10: begin
                           // synthesis translate_off
                           $display("riscv wrote Z80 input data 0x%02x", rv_wdata);
                           // synthesis translate_on
                            z80di <= rv_wdata;
                            io_port_status <= IO_STAT_READY;
                        end
                        4'd12: font_fg_color <= rv_wdata;
                        4'd13: font_bg_color <= rv_wdata;
                        4'd14: sound_ctrl <= rv_wdata[1:0];
                        4'd15: z80_mhz <= rv_wdata[4:0];
                    endcase
                 end
                 else begin
                    case (rv_adr)
                        4'd0: rv_rdata <= {16'd0, console_status};
                        4'd1: rv_rdata <= {16'd0, disk_drive};
                        4'd2: rv_rdata <= {16'd0, disk_track};
                        4'd3: rv_rdata <= {16'd0, disk_sector_lsb};
                        4'd5: rv_rdata <= {16'd0, disk_dma_adr_lsb};
                        4'd6: rv_rdata <= {16'd0, disk_dma_adr_msb};
                        4'd7: rv_rdata <= {16'd0, disk_sector_msb};
                        4'd8: rv_rdata <= {16'd0, io_port_adr};
                        4'd9: begin
                            // synthesis translate_off
                            $display("riscv read Z80 output data 0x%02x", out_port_data);
                            // synthesis translate_on
                            rv_rdata <= {16'd0, out_port_data};
                            io_port_status <= IO_STAT_READY;
                        end
                        4'd11: rv_rdata <= {z80hlt, 21'd0, io_port_status};
                        4'd12: rv_rdata <= font_fg_color;
                        4'd13: rv_rdata <= font_bg_color;
                        4'd14: rv_rdata <= {22'd0, sound_ctrl};
                        4'd15: rv_rdata <= {19'd0, z80_mhz};
                        default: rv_rdata <= 24'd0;
                    endcase
                 end
            end
            if (z80_iord) begin
                 case (z80adr)
                     8'd0: begin
                        z80di <= console_status;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd10: begin
                        z80di <= disk_drive;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd11: begin
                        z80di <= disk_track;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd12: begin
                        z80di <= disk_sector_lsb;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd15: begin
                        z80di <= disk_dma_adr_lsb;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd16: begin
                        z80di <= disk_dma_adr_msb;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd17: begin
                        z80di <= disk_sector_msb;
                        io_port_status <= IO_STAT_READY;
                     end
                     default: if (io_port_status == IO_STAT_IDLE) begin
                        // synthesis translate_off
                        $display("Z80 input port 0x%02x", z80adr);
                        // synthesis translate_on
                         io_port_adr <= z80adr;
                         io_port_status <= IO_STAT_READ;
                     end
                 endcase
             end
             else if (z80_iowr) begin
                 case (z80adr)
                     8'd10: begin
                        disk_drive <= z80do;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd11: begin
                        disk_track <= z80do;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd12: begin
                        disk_sector_lsb <= z80do;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd15: begin
                        disk_dma_adr_lsb <= z80do;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd16: begin
                        disk_dma_adr_msb <= z80do;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'd17: begin
                        disk_sector_msb <= z80do;
                        io_port_status <= IO_STAT_READY;
                     end
                     8'h32: begin
                        beeper <= z80do[0];
                        io_port_status <= IO_STAT_READY;
                     end
                     default: if (io_port_status == IO_STAT_IDLE) begin
                         // synthesis translate_off
                         $display("Z80 output 0x%02x to port 0x%02x",z80do,z80adr);
                         // synthesis translate_on
                         io_port_status <= IO_STAT_WRITE;
                         io_port_adr <= z80adr;
                         out_port_data <= z80do;
                     end
                 endcase
             end
             else begin
                io_port_status <= IO_STAT_IDLE;
                z80_io_ready <= 0;
             end
             if (io_port_status == IO_STAT_READY)
                 z80_io_ready <= 1;
             else
                 z80_io_ready <= 0;
             if (z80_reset)
                 beeper <= 1'b0;   // a reset silences it, as it would the ITT
        end
    end

    // Z80 speed.  The Z80 is clocked at 25 MHz, the clock of this module; to
    // run it slower (programs that time things with loops, written for a
    // 4 MHz machine), only some of its clocks are enabled: each clock adds
    // z80_mhz to an accumulator, and a clock is enabled each time it passes
    // 25.  So z80_mhz = 4 enables 4 of every 25 clocks, 4.000 MHz on average.
    // 0, or 25 and more, enables every clock.  The memory and I/O handshakes
    // are level based and simply last longer.
    reg [4:0] z80_cen_acc;
    wire z80_full_speed = z80_mhz == 5'd0 || z80_mhz >= 5'd25;
    wire [5:0] z80_cen_sum = z80_cen_acc + z80_mhz;
    always @(posedge clk) begin
        if (reset || z80_full_speed) begin
            z80_cen_acc <= 5'd0;
            z80_cen <= 1'b1;
        end
        else if (z80_cen_sum >= 6'd25) begin
            z80_cen_acc <= z80_cen_sum - 6'd25;
            z80_cen <= 1'b1;
        end
        else begin
            z80_cen_acc <= z80_cen_sum[4:0];
            z80_cen <= 1'b0;
        end
    end
endmodule
