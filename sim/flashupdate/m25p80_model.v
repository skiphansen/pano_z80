// Behavioural model of a Numonyx M25P80 (1 MB SPI flash), SPI mode 0.
// Enforces the rules the real part does: SE/PP need a preceding WREN, every
// command except RDSR is ignored while a write is in progress, PP can only
// clear bits and wraps inside its 256 byte page.
`timescale 1ns / 1ps
module m25p80_model #(
    parameter integer T_SE_NS = 400000,   // sector erase busy time (shortened)
    parameter integer T_PP_NS = 60000     // page program busy time (shortened)
) (
    input  wire csb,
    input  wire clk,
    input  wire di,
    output reg  dout
);
    reg [7:0] mem [0:1048575];
    reg [7:0] page [0:255];
    reg [7:0] sr, cmd, obyte;
    reg [23:0] addr;
    integer bitcnt, bytecnt, obit, pagecnt, k;
    reg out_en, wel;
    time busy_until;
    integer n_se, n_pp, n_wren, n_rdid, n_ignored_busy, n_ignored_nowel, n_read;

    initial begin
        dout = 1'b1; wel = 0; busy_until = 0; out_en = 0; bitcnt = 0; bytecnt = 0;
        n_se = 0; n_pp = 0; n_wren = 0; n_rdid = 0; n_read = 0;
        n_ignored_busy = 0; n_ignored_nowel = 0; cmd = 0; obit = 0;
    end

    function [7:0] next_out;
        input dummy;
        begin
            case (cmd)
                8'h9F: next_out = (bytecnt == 1) ? 8'h20 : (bytecnt == 2) ? 8'h20 : 8'h14;
                8'h05: next_out = {6'b0, wel, ($time < busy_until)};
                8'h03: next_out = mem[addr[19:0]];
                default: next_out = 8'hFF;
            endcase
        end
    endfunction

    always @(negedge csb) begin
        bitcnt = 0; bytecnt = 0; out_en = 0; obit = 0; pagecnt = 0; cmd = 8'h00;
    end

    always @(posedge clk) if (!csb) begin
        sr = {sr[6:0], di};
        bitcnt = bitcnt + 1;
        if (bitcnt == 8) begin
            bitcnt = 0;
            if (bytecnt == 0) begin
                cmd = sr;
                if (($time < busy_until) && cmd != 8'h05) begin
                    n_ignored_busy = n_ignored_busy + 1;
                    cmd = 8'h00;
                end
                if (cmd == 8'h9F) n_rdid = n_rdid + 1;
                if (cmd == 8'h9F || cmd == 8'h05) begin out_en = 1; obit = 0; end
            end else if (cmd == 8'h03 || cmd == 8'h02 || cmd == 8'hD8) begin
                if (bytecnt <= 3) begin
                    addr = {addr[15:0], sr};
                    if (bytecnt == 3 && cmd == 8'h03) begin
                        out_en = 1; obit = 0; n_read = n_read + 1;
                    end
                end else if (cmd == 8'h02) begin
                    page[(addr[7:0] + pagecnt) & 8'hFF] = sr;
                    pagecnt = pagecnt + 1;
                end
            end
            bytecnt = bytecnt + 1;
        end
    end

    always @(negedge clk) if (!csb && out_en) begin
        if (obit == 0) begin
            obyte = next_out(1'b0);
            if (cmd == 8'h03) addr = addr + 1;
        end
        dout = obyte[7];
        obyte = {obyte[6:0], 1'b1};
        obit = (obit + 1) % 8;
    end

    always @(posedge csb) begin
        dout = 1'b1;
        if (bitcnt == 0) case (cmd)
            8'h06: if (bytecnt == 1) begin wel = 1; n_wren = n_wren + 1; end
            8'h04: if (bytecnt == 1) wel = 0;
            8'hD8: if (bytecnt == 4) begin
                if (!wel) n_ignored_nowel = n_ignored_nowel + 1;
                else begin
                    for (k = 0; k < 65536; k = k + 1) mem[{addr[19:16], 16'h0} + k] = 8'hFF;
                    busy_until = $time + T_SE_NS; wel = 0; n_se = n_se + 1;
                end
            end
            8'h02: if (bytecnt > 4) begin
                if (!wel) n_ignored_nowel = n_ignored_nowel + 1;
                else begin
                    for (k = 0; k < pagecnt && k < 256; k = k + 1)
                        mem[{addr[19:8], 8'h0} + ((addr[7:0] + k) & 8'hFF)] =
                            mem[{addr[19:8], 8'h0} + ((addr[7:0] + k) & 8'hFF)] &
                            page[(addr[7:0] + k) & 8'hFF];
                    busy_until = $time + T_PP_NS; wel = 0; n_pp = n_pp + 1;
                end
            end
            default: ;
        endcase
    end
endmodule
