`timescale 10 ns/1 ps

module test;
    reg tb_clk;
    reg tb_reset;

    parameter CLK_HALF_PERIOD = 2;
    parameter CLK_PERIOD      = 2 * CLK_HALF_PERIOD;

    pano_top_tb DUT(
        .clk_4(tb_clk),
        .reset(tb_reset)
    );

    always #CLK_HALF_PERIOD tb_clk = !tb_clk;

    task reset();
        begin
            tb_reset <= 1;
            #(10 * CLK_PERIOD); // DCM_SP needs at least 3 clock periods
            tb_reset <= 0;
        end
    endtask

    initial begin
        tb_clk <= 0;
        tb_reset <= 1;

        #CLK_PERIOD;

        reset();

        #(640*480*5 * CLK_PERIOD);
    end

endmodule

`define Z80_RAM_2K

module pano_top_tb (
    // Global Clock Input
    input wire clk_4,
    input wire reset
    );
    
    // ----------------------------------------------------------------------
    // T80 CPU core
    wire [7:0] z80di;
    wire [7:0] z80do;
    wire [15:0] z80adr;
    wire [7:0] z80_io_read_data;
    wire z80_M1_n;
    wire z80_MREQ_n;
    wire z80_IORQ_n;
    wire z80_RD_n;
    wire z80_WR_n;
    wire z80_RFSH_n;
    wire z80_HALT_n;
    wire z80_BUSAK_n;
    wire z80_Ready;
    wire io_ready;
    
    T80sed T80sed(
        .RESET_n(!reset),
        .CLK_n(clk_4),
        .CLKEN(1'b1),
        .WAIT_n(z80_Ready),
        .INT_n(1'b1),
        .NMI_n(1'b1),
        .BUSRQ_n(1'b1),
        .DI(z80di),
        .DO(z80do),
        .A(z80adr),
        .M1_n(z80_M1_n),
        .MREQ_n(z80_MREQ_n),
        .IORQ_n(z80_IORQ_n),
        .RD_n(z80_RD_n),
        .WR_n(z80_WR_n),
        .RFSH_n(z80_RFSH_n),
        .HALT_n(z80_HALT_n),
        .BUSAK_n(z80_BUSAK_n)
    );

    wire z80_io_wr = !z80_IORQ_n && !z80_WR_n;
    wire z80_io_rd = !z80_IORQ_n && !z80_RD_n;
    wire z80_mem_wr = !z80_MREQ_n && !z80_WR_n;
    wire z80_mem_rd = (!z80_MREQ_n || !z80_M1_n) && !z80_RD_n;
    wire z80_ram_valid;
    wire z80_io_valid;
    wire [7:0] z80ram_do;
    wire [7:0] z80ram_do_b;
    wire [7:0] z80io_rdata;
    wire [3:0] mem_wstrb;
    wire [31:0] mem_addr;
    wire [31:0] mem_wdata;

    // RAMB16_S9_S9: Spartan-3/3E/3A/3AN/3AD 2k x 8 + 1 Parity bit Dual-Port RAM
    // Xilinx HDL Libraries Guide, version 11.2
    RAMB16_S9_S9 #(
    .INIT_A(9'h000), // Value of output RAM registers on Port A at startup
    .INIT_B(9'h000), // Value of output RAM registers on Port B at startup
    .SRVAL_A(9'h000), // Port A output value upon SSR assertion
    .SRVAL_B(9'h000), // Port B output value upon SSR assertion
    .WRITE_MODE_A("WRITE_FIRST"), // WRITE_FIRST, READ_FIRST or NO_CHANGE
    .WRITE_MODE_B("WRITE_FIRST"), // WRITE_FIRST, READ_FIRST or NO_CHANGE
    .SIM_COLLISION_CHECK("ALL"), // "NONE", "WARNING_ONLY", "GENERATE_X_ONLY", "ALL"
    .INIT_00(256'hd30025cab77e000321000a0d21646c726f77206f6e6150206f6c6c65480017c3),
    .INIT_01(256'h0000000000000000000000000000000000000000000000000076f3001ac32301)
    ) RAMB16_S9_S9_inst (
    .DOA(z80ram_do), // Port A 8-bit Data Output
    // .DOB(z80ram_do_b), // Port B 8-bit Data Output
    // .DOPA(DOPA), // Port A 1-bit Parity Output
    // .DOPB(DOPB), // Port B 1-bit Parity Output
    .ADDRA(z80adr[10:0]), // Port A 11-bit Address Input
    .ADDRB(11'b0), // Port B 11-bit Address Input
    .CLKA(clk_4), // Port A Clock
    .CLKB(1'b0), // Port B Clock
    .DIA(z80do), // Port A 8-bit Data Input
    .DIB(8'b0), // Port B 8-bit Data Input
    .DIPA(1'b0), // Port A 1-bit parity Input
    .DIPB(1'b0), // Port-B 1-bit parity Input
    .ENA(1'b1), // Port A RAM Enable Input
    .ENB(1'b1), // Port B RAM Enable Input
    .SSRA(reset), // Port A Synchronous Set/Reset Input
    .SSRB(reset), // Port B Synchronous Set/Reset Input
    .WEA(z80_mem_wr), // Port A Write Enable Input
    .WEB(1'b0) // Port B Write Enable Input
    );

    assign z80di = !z80_IORQ_n ? z80_io_read_data : z80ram_do;

    // ----------------------------------------------------------------------
    // PicoRV32
    
    // Memory Map
    // 03000200 - 030002FF Z80 I/O       (256B)
    // FFFFF800 - FFFFFFFF Internal RAM  (2KB)
    parameter [31:0] STACKADDR = 32'hfffffffc;
    parameter [31:0] PROGADDR_RESET = 32'hFFFFF800;
    parameter [31:0] PROGADDR_IRQ = 32'hFFFFF808;
    
    wire mem_valid;
    wire mem_instr;
    wire mem_ready;
    wire [31:0] mem_rdata;
    wire [31:0] mem_la_addr;
    
    reg cpu_irq;
    
    wire la_addr_in_ram = (mem_la_addr >= 32'hFFFFF800);
    wire la_addr_in_z80_io = (mem_la_addr >= 32'h03000200) && (mem_la_addr < 32'h030002ff);
    
    reg addr_in_ram;
    reg addr_in_z80_io;
    
    always@(posedge clk_4) begin
        addr_in_ram <= la_addr_in_ram;
        addr_in_z80_io <= la_addr_in_z80_io;
    end
    
    wire ram_valid = (mem_valid) && (!mem_ready) && (addr_in_ram);
    assign z80_io_valid = (mem_valid) && (addr_in_z80_io);
    wire general_valid = (mem_valid) && (!mem_ready);
    
    reg default_ready;
    
    always @(posedge clk_4) begin
        default_ready <= general_valid;
    end
    
    wire uart_ready;
    assign mem_ready = default_ready;
    
    reg mem_valid_last;
    always @(posedge clk_4) begin
        mem_valid_last <= mem_valid;
        if (mem_valid && !mem_valid_last && !(ram_valid || z80_io_valid))
            cpu_irq <= 1'b1;
        //else
        //    cpu_irq <= 1'b0;
        if (!reset)
            cpu_irq <= 1'b0;
    end
    
    wire [31:0] ram_rdata;
        
    picorv32 #(
        .STACKADDR(STACKADDR),
        .PROGADDR_RESET(PROGADDR_RESET),
        .ENABLE_IRQ(1),
        .ENABLE_IRQ_QREGS(0),
        .ENABLE_IRQ_TIMER(0),
        .COMPRESSED_ISA(1),
        .PROGADDR_IRQ(PROGADDR_IRQ),
        .MASKED_IRQ(32'hfffffffe),
        .LATCHED_IRQ(32'hffffffff)
    ) cpu (
        .clk(clk_4),
        .resetn(!reset),
        .mem_valid(mem_valid),
        .mem_instr(mem_instr),
        .mem_ready(mem_ready),
        .mem_addr(mem_addr),
        .mem_wdata(mem_wdata),
        .mem_wstrb(mem_wstrb),
        .mem_rdata(mem_rdata),
        .mem_la_addr(mem_la_addr),
        .irq({31'b0, cpu_irq})
    );

    // RAMB16_S36: Spartan-3/3E 512 x 32 + 4 Parity bits Single-Port RAM
    // WARNING: byte writes are not supported!  All writes are 32 bit 
    RAMB16_S36 #(
    .INIT(36'h000000000), // Value of output RAM registers at startup
    .SRVAL(36'h000000000), // Output value upon SSR assertion
    .WRITE_MODE("WRITE_FIRST"), // WRITE_FIRST, READ_FIRST or NO_CHANGE
    .INIT_00(256'h07b7a00100a080e700000097010080e70000009700018513000100010001a811),
    .INIT_01(256'h4605030007370550059320e784230ff777132047c70320e78223055007130300),
    .INIT_02(256'h22b7042300158793fed79ae3fec78ae30ff7f79322c7478322474783a0194689),
    .INIT_03(256'h0000000000000000000000000000000000000000000000000000b7d50ff7f593)
    ) RAMB16_S36_inst (
    .DO(ram_rdata), // 32-bit Data Output
    //.DOP(DOP), // 4-bit parity Output
    .ADDR(mem_addr[10:2]), // 9-bit Address Input
    .CLK(clk_4), // Clock
    .DI(mem_wdata), // 32-bit Data Input
    .DIP(4'h0), // 4-bit parity Input
    .EN(1'b1), // RAM Enable Input
    .SSR(reset), // Synchronous Set/Reset Input
    .WE(mem_wstrb[0]) // Write Enable Input
    );

    // Z80 I/O
    // 

    assign z80_Ready = !z80_MREQ_n || (!z80_IORQ_n && io_ready);

    assign mem_rdata = 
        addr_in_ram ? ram_rdata : (
        addr_in_z80_io ? {24'b0, z80io_rdata} : (
        32'hFFFFFFFF));

    cpm_io cpm_io(
        .clk(clk_4),
     // Z80 interface
        .z80_iord(z80_io_rd),
        .z80_iowr(z80_io_wr),
        .z80adr(z80adr[7:0]),
        .z80di(z80_io_read_data),
        .z80do(z80do),
        .z80_io_ready(io_ready),

    // RISC V interface
        .io_valid(z80_io_valid),
        .rv_wdata(mem_wdata[7:0]),
        .rv_adr(mem_addr[5:2]),
        .rv_wstr(mem_wstrb[0]),
        .rv_rdata(z80io_rdata)
    );

endmodule

