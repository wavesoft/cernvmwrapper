#!/usr/bin/perl
#######################################################################
#  Hypervisor-Virtual machine bi-directional communication
#  through floppy disk.
#######################################################################
#
#  This script implements the reading part for the guest-side.
#  Use the FloppyIO Class from the hypervisor-side to send data.
#
#  This script writes the output at STDOUT and all the errors to STDERR,
#  so you can use it like this:
#
#  ./read.pl > my_config.sh
#
#  or like this:
#
#  ./read.pl > my_config.sh 2>/dev/null
#
#======================================================================
#  
#  Here is the layout of the floppy disk image (Example of 28k):
#
#  +-----------------+------------------------------------------------+
#  | 0x0000 - 0x37FF |  Hypervisor -> Guest Buffer                    |
#  | 0x3800 - 0x6FFE |  Guest -> Hypervisor Buffer                    |
#  |     0x6FFF      |  "Data available for guest" flag byte          |
#  |     0x7000      |  "Data available for hypervisor" flag byte     |
#  +-----------------+------------------------------------------------+
#
#======================================================================
#
# Created on November 24, 2011, 12:30 PM
# Author: Ioannis Charalampidis <ioannis.charalampidis AT cern DOT ch>
# License: GNU/LGPL Version 3
#
# This file is part Floppy I/O, a Virtual Machine - Hypervisor intercommunication system.
# Copyright (C) 2011 Ioannis Charalampidis 
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#######################################################################

use strict;
use warnings;

# ==[ CONFIGURATION ]====================
my $FLOPPY = "/dev/fd0";
my $FLOPPY_SIZE = 28672;
# =======================================

# Calculate buffer positions
my $IN_OFS=0; my $IN_SIZE=$FLOPPY_SIZE/2-1;

# Try to open file for input
open FD, "+<$FLOPPY" or die $!;
binmode FD;

# The following serves for 2 purposes:
# 1) Force the OS to actually read the floppy device 
#    (Because there is the case that the first 12K are just cached)
# 2) Notifies the server that the client has read the data
seek FD, $FLOPPY_SIZE-2,0; # -2=out (client), -1=in (server) (Control bytes)
print FD "\x00";

# Go back to the input position
seek FD, $IN_OFS, 0;

# Read everything, stop at first null byte
my ($buf, $data, $n);
while (($n = read FD, $data, 1) != 0) {
    if ($data eq "\0") {
        last;
    }

    # Echo each byte as it arrives
    print $data;
}

# Close FD when done
close FD;

