module dualport_ram #(
    parameter integer WORDS = 2400,
    parameter integer WIDTH = 8
) (
    clka,
    wea,
    addra,
    dina,
    clkb,
    addrb,
    doutb
);

    input clka;
    input wea;
    input [11 : 0] addra;
    input [WIDTH-1 : 0] dina;
    input clkb;
    input [11 : 0] addrb;
    output reg [WIDTH-1 : 0] doutb;

    reg [WIDTH-1:0] ram [0:WORDS-1];
    
    always@(posedge clka) begin
        if (wea)
            ram[addra] <= dina;
    end
    
    always@(posedge clkb) begin
        doutb <= ram[addrb];
    end

endmodule
