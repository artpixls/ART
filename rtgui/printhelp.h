// -*- C++ -*-
#pragma once

#include "options.h"
#include <iostream>


inline void ART_print_help(std::ostream &out, const char *progname, bool gui)
{
    auto pn = Glib::path_get_basename(progname);
    
    out << "  An advanced, cross-platform program for developing raw photos." << std::endl;
    out << std::endl;
    out << "  Website: https://art.pixls.us/" << std::endl;
    out << std::endl;
    out << "Symbols:" << std::endl;
    out << "  <Chevrons> indicate parameters you can change." << std::endl;
    out << "  [Square brackets] mean the parameter is optional." << std::endl;
    out << "  The pipe symbol | indicates a choice of one or the other." << std::endl;
    out << "  The dash symbol - denotes a range of possible values from one to the other." << std::endl;
    out << std::endl;
    
    if (gui) {
        out << "Usage:\n";
        out << "  " << pn << " <folder> Start File Browser inside folder.\n";
        out << "  " << pn << " <file>   Start Image Editor with file.\n\n";
        out << "Options:\n";
#ifdef WIN32
        out << "  -w Do not open the Windows console\n";
#endif
        out << "  -v Print version number and exit\n";
        out << "  -N Force opening a new app instance\n";
        // out << "  -s Simple editor mode\n";
        out << "  -S <file> Start with session from file\n"
            << "  -Sc Clear the session\n"
            << "  -Sa <file1> [... <fileN>] Add the files to the session\n"
            << "  -Sr <file1> [... <fileN>] Remove the files from the session\n";
        out << "  -h -? Display this help message\n";
    } else {        
        Glib::ustring pparamsExt = paramFileExtension.substr(1);
        out << "Usage:" << std::endl;
        out << "  " << pn << " -c <dir>|<files>   Convert files in batch with default parameters." << std::endl;
        out << "  " << pn << " <other options> -c <dir>|<files>   Convert files in batch with your own settings." << std::endl;
        out << "  " << pn << " --make-icc <make-icc options>   Build an ICC output color profile." << std::endl;
        out << "  " << pn << " --check-lut <lut-filename>   Check the validity of the given LUT file." << std::endl;
        out << std::endl;
        out << "Options:" << std::endl;
        out << "  " << pn << "[-o <output>|-O <output>] [-q] [-a] [-s|-S] [-p <one" << paramFileExtension << "> [-p <two" << paramFileExtension << "> ...] ] [-d] [ -j[1-100] -js<1-3> | -t[z] -b<8|16|16f|32> | -n -b<8|16> | -Ttype ] [-Y] [-f] -c <input>" << std::endl;
        out << std::endl;
        out << "  -c <files>       Specify one or more input files or folders. When specifying\n"
            << "                   folders, ART will look for image file types which comply with\n"
            << "                   the selected extensions (see also '-a').\n"
            << "                   '-c' must be the last option." << std::endl;
        out << "  -o <file>|<dir>  Set output file or folder. Saves output file alongside input\n"
            << "                   file if -o is not specified." << std::endl;
        out << "  -O <file>|<dir>  Set output file or folder and copy " << pparamsExt << " file into it." << std::endl;
        out << "                   Saves output file alongside input file if -O is not\n"
            << "                   specified." << std::endl;
        out << "  -q               Quick-start mode. Does not load cached files to speedup\n"
            << "                   start time." << std::endl;
        out << "  -a               Process all supported image file types when specifying\n"
            << "                   a folder, even those not currently selected in\n"
            << "                   Preferences > File Browser > Parsed Extensions." << std::endl;
        out << "  -s               Use the existing sidecar file to build the processing\n"
            << "                   parameters, e.g. for photo.raw there should be a\n"
            << "                   photo.raw." << pparamsExt << " file in the same folder. If the sidecar file\n"
            << "                   does not exist, neutral values will be used." << std::endl;
        out << "  -S               Like -s but skip if the sidecar file does not exist." << std::endl;
        out << "  -p <file" << paramFileExtension << ">    Specify processing profile to be used for all conversions." << std::endl;
        out << "                   You can specify as many sets of \"-p <file" << paramFileExtension << ">\" options\n"
            << "                   as you like, each will be built on top of the previous one,\n"
            << "                   as explained below." << std::endl;
        out << "  -d               Use the default raw or non-raw processing profile as set in" << std::endl;
        out << "                   Preferences > Image Processing > Default Processing Profile" << std::endl;
        out << "  -j[1-100]        Specify output to be JPEG (default, if -t and -n are not set)." << std::endl;
        out << "                   Optionally, specify compression 1-100 (default value: 92)." << std::endl;
        out << "  -js<1-3>         Specify the JPEG chroma subsampling parameter, where:" << std::endl;
        out << "                   1 = Best compression:   2x2, 1x1, 1x1 (4:2:0)" << std::endl;
        out << "                       Chroma halved vertically and horizontally." << std::endl;
        out << "                   2 = Balanced (default): 2x1, 1x1, 1x1 (4:2:2)" << std::endl;
        out << "                       Chroma halved horizontally." << std::endl;
        out << "                   3 = Best quality:       1x1, 1x1, 1x1 (4:4:4)" << std::endl;
        out << "                       No chroma subsampling." << std::endl;
        out << "  -b<8|16|16f|32>  Specify bit depth per channel." << std::endl;
        out << "                   8   = 8-bit integer. Applies to JPEG, PNG and TIFF.\n"
            << "                   Default for JPEG and PNG." << std::endl;
        out << "                   16  = 16-bit integer. Applies to TIFF and PNG.\n"
            << "                   Default for TIFF." << std::endl;
        out << "                   16f = 16-bit float. Applies to TIFF." << std::endl;
        out << "                   32  = 32-bit float. Applies to TIFF." << std::endl;
        out << "  -t[z]            Specify output to be TIFF." << std::endl;
        out << "                   Uncompressed by default, or deflate compression with 'z'." << std::endl;
        out << "  -n               Specify output to be compressed PNG." << std::endl;
        out << "                   Compression is hard-coded to PNG_FILTER_PAETH, Z_RLE." << std::endl;
        out << "  -Ttype           Select the given (custom) output type. Must be supported\n"
            << "                   by a user-defined custom image saver." << std::endl;
        out << "  -Y               Overwrite output if present." << std::endl;
        out << "  -f               Use the custom fast-export processing pipeline." << std::endl;
        out << "  -V               Verbose output." << std::endl;
        out << "  --progress       Show progress info in a format compatible with zenity." << std::endl;
        out << std::endl;
        out << "Your " << pparamsExt << " files can be incomplete, ART will build the final values as follows:" << std::endl;
        out << "  1- A new processing profile is created using neutral values," << std::endl;
        out << "  2- If the \"-d\" option is set, the values are overridden by those found in" << std::endl;
        out << "     the default raw or non-raw processing profile." << std::endl;
        out << "  3- If one or more \"-p\" options are set, the values are overridden by those" << std::endl;
        out << "     found in these processing profiles." << std::endl;
        out << "  4- If the \"-s\" or \"-S\" options are set, the values are finally overridden\n"
            << "     by those found in the sidecar files." << std::endl;
        out << "  The processing profiles are processed in the order specified on the\n"
            << "  command line." << std::endl;
    }
}

inline void ART_print_help(const char *progname, bool gui)
{
    ART_print_help(std::cout, progname, gui);
}
