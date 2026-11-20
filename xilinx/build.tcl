# Build the Pano Z80 bitstream from the command line and stamp it with a
# build ID.
#
#   cd xilinx && xtclsh build.tcl
#
# The build ID is the build time (seconds since 1970, UTC).  It is passed to
# the design as the BUILD_ID parameter of pano_top, where the firmware can read
# it, and to bitgen as the UserID, which ends up in the header of the .bit
# file.  The USB updater (F11) shows both, so it can tell you which bitstream
# is running and which one an update would install.
#
# The project is opened through a copy so that ISE does not rewrite the
# tracked pano_z80.xise.

set id [clock seconds]
file copy -force pano_z80.xise pano_z80_build.xise
project open pano_z80_build.xise
project set "Generics, Parameters" "BUILD_ID=$id" -process "Synthesize - XST"
project set "UserID Code (8 Digit Hexadecimal)" [format "0x%08X" $id] -process "Generate Programming File"
if {[catch {process run "Generate Programming File" -force rerun_all} err]} {
    puts "PROCESS ERROR: $err"
}
puts "STATUS synth: [process get "Synthesize - XST" status]"
puts "STATUS map:   [process get "Map" status]"
puts "STATUS par:   [process get "Place & Route" status]"
puts "STATUS bit:   [process get "Generate Programming File" status]"
puts [format "BUILD_ID 0x%08X = %s UTC" $id [clock format $id -gmt 1 -format "%Y-%m-%d %H:%M:%S"]]
project close
