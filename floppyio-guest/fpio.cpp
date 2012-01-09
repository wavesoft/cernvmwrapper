// This file is part Floppy I/O, a Virtual Machine - Hypervisor intercommunication system.
// Copyright (C) 2011 Ioannis Charalampidis 
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// -------------------------------------------------------------------
// File:   fpio.cpp
// Author: Ioannis Charalampidis <ioannis.charalampidis AT cern DOT ch>
// License: GNU General Public License - Version 3.0
// -------------------------------------------------------------------
//
// -------------------------------------------------------------------
// Application front-end for the FloppyIO Library. Examples of use:
// -------------------------------------------------------------------
// 
// 1) Create a floppy disk image (from the hypervisor)
//
//   fpclient -zH /var/vmware/myvm/floppy.img
//
// 2) Send data from STDIN:
//
//   Hypervisor:  fpclient -H -s /var/vmware/myvm/floppy.img < (data)
//        Guest:  fpclient -r > (handler)
//
// 3) Send data from a specified file from the guest to hypervisor:
//
//   Hypervisor:  fpclient -H -r /var/vmware/myvm/floppy.img > (data)
//        Guest:  fpclient -S (filename)
//
// 4) Send data from the guest to the hypervisor using a block device
//    different than /dev/fd0
//
//   Hypervisor:  fpclient -H -r /var/vmware/myvm/floppy.img > (data)
//        Guest:  fpclient -S (filename) /dev/fd1
//
// -------------------------------------------------------------------
// 
// Created at January 9, 2012, 17:26 PM

#include <cxxabi.h>
#include <exception>
#include <iostream>

#include "../floppyIO.h";

using namespace std;

#define  MODE_SEND      1
#define  MODE_RECEIVE   2
#define  MODE_ZEROONLY  3

//
// Help screen
//
void help() {
    printf("FloppyIO Hypervisor-Guest Communication System - Library version: %i.%i\n", FPIO_VERSION);
    printf("Usage: fpio [-hsrcH] [-z] [-R [filename] | -S [filename]] [-t timeout] [floppy]\n");
    printf("Description:\n");
    printf("  floppy        The block device to use for FloppyIO (Default: /dev/fd0).\n");
    printf("  -c            Use character instead of binary mode (Compatible with the older perl clients)\n");
    printf("  -H            Hypervisor mode. Use this option if you run FloppyIO from the hypervisor.\n");
    printf("  -z            Zero-out (reset) floppy file.\n");
    printf("  -s            Read data from STDIN and send them.\n");
    printf("  -S filename   Read data from the specified file and send them.\n");
    printf("  -r            Receive data and write them on STDOUT.\n");
    printf("  -R filename   Receive data and save them to the specified file.\n");
    printf("  -t timeout    The time to wait for synchronization. If not specified, waits for ever.\n");
    printf("  -h            Show this help screen.\n");
};

//
// Display an error message and exit.
//
// @param message   The error message
// @param code      The error code (this is also the application's exit code)
//
void error(const char * message, int code) {
    fprintf(stderr, "## FLOPPY I/O ERROR\n## Message: %s\n## Error code = %i\n", message, code);
    exit(code);
};

//
// Main application
//
int main(int argc, char **argv) {
    char c;
    char * file = (char *)"/dev/fd0";
    char * iofile = NULL;
    int flags = FPIO_SYNCHRONIZED | FPIO_BINARY | FPIO_CLIENT | FPIO_NOINIT | FPIO_NOCREATE;
    int mode = 0, timeout = 0, res;

    //
    // Parse the command-line arguments
    //
    while ((c = getopt (argc, argv, "zhcHsrS:R:f:t:")) != -1)
        switch (c) {
            case 'c':
                flags &= ~FPIO_BINARY;
                break;
                
            case 'z':
                flags &= ~FPIO_NOINIT;
                flags &= ~FPIO_NOCREATE;
                if (mode == 0) mode=MODE_ZEROONLY;
                break;
                
            case 'h':
                help();
                return 2;
                break;
                
            case 'H':
                flags &= ~FPIO_CLIENT;
                break;
                
            case 'S':
                iofile = optarg;
                printf("Optarg for S: %s\n",optarg);
                mode = MODE_SEND;
                break;
                
            case 'R':
                iofile = optarg;
                printf("Optarg for R: %s\n",optarg);
                mode = MODE_RECEIVE;
                break;
                
            case 's':
                mode = MODE_SEND;
                break;
                
            case 'r':
                mode = MODE_RECEIVE;
                break;

            case 't':
                timeout = atoi(optarg);
                break;
                
            case '?':
                if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
                return 1;
                
            default:
                help();
                return 2;
        
        }

    if (mode == 0) {
        fprintf (stderr, "No mode specified! Please specify one of the -S/-s, the -R/-r or the -z option!\n", optopt);
        return 1;
    }

    if (argc == optind+1) {
        file = argv[argc-1];
    } else if (argc > optind) {
        fprintf (stderr, "Unrecognized trailing arguments!\n", optopt);
        return 1;        
    }

    // Create a FloppyIO class with the specified floppy device and flags
    FloppyIO fio ( file, flags );
    if (!fio.ready()) error(fio.errorStr.c_str(), fio.error);

    // Set synchronization timeout
    fio.syncTimeout=timeout;

    // Build the appropriate streams for each mode
    if (mode == MODE_SEND) {
        istream * ins = &cin;
        if (iofile != NULL) ins = new ifstream( iofile, ifstream::in );

        // Send stream
        fio.send(ins);
        if (!fio.ready()) error(fio.errorStr.c_str(), fio.error);

        // Close stream
        if (iofile != NULL) ((ifstream*)ins)->close();
        
    } else if (mode == MODE_RECEIVE) {
        ostream * outs = &cout;
        if (iofile != NULL) outs = new ofstream( iofile, ofstream::out | ofstream::trunc );

        // Send stream
        fio.receive(outs);
        if (!fio.ready()) error(fio.errorStr.c_str(), fio.error);

        // Close stream
        if (iofile != NULL) ((ofstream*)outs)->close();
        
    }

    return 0;
}
