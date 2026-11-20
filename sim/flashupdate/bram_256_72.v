// Behavioural stand-in for fpga/mig/bram_256_72.v (RAMB16, WRITE_FIRST)
module bram_256_72(
    input wire clk, input wire rst, input wire [7:0] addr, input wire en,
    input wire we, output reg [71:0] rd, input wire [71:0] wr);
    reg [71:0] mem [0:255];
    integer i;
    initial begin for (i = 0; i < 256; i = i + 1) mem[i] = 72'd0; rd = 72'd0; end
    always @(posedge clk) begin
        if (rst) rd <= 72'd0;
        else if (en) begin
            if (we) begin mem[addr] <= wr; rd <= wr; end
            else rd <= mem[addr];
        end
    end
endmodule
