/* GNU ed - The GNU line editor.
   Copyright (C) 2006-2022 Antonio Diaz Diaz.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
   Exit status: 0 for a normal exit, 1 for environmental problems
   (file not found, invalid flags, I/O errors, etc), 2 to indicate a
   corrupt or invalid input file, 3 for an internal consistency error
   (e.g., bug) which caused ed to panic.
*/
/*
 * CREDITS
 *
 *      This program is based on the editor algorithm described in
 *      Brian W. Kernighan and P. J. Plauger's book "Software Tools
 *      in Pascal", Addison-Wesley, 1981.
 *
 *      The buffering algorithm is attributed to Rodney Ruddock of
 *      the University of Guelph, Guelph, Ontario.
 *
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <iostream>
#include <span>

#include <sys/stat.h>

#include "carg_parser.h"
#include "ed.h"

using namespace std;


/* if set, use EREs */
auto extended_regexp( bool set, bool new_val ) -> bool {
  static bool extended_regexp = false;

  if( set ) { extended_regexp = new_val; }

  return extended_regexp;
}


/* if set, run in restricted mode */
auto restricted( bool set, bool new_val ) -> bool {
  static bool restricted = false;

  if( set ) { restricted = new_val; }

  return restricted;
}


/* if set, suppress diagnostics, byte counts and '!' prompt */
auto scripted( bool set, bool new_val ) -> bool {
  static bool scripted = false;

  if( set ) { scripted = new_val; }

  return scripted;
}


/* if set, strip trailing CRs */
auto strip_cr( bool set, bool new_val ) -> bool {
  static bool strip_cr = false;

  if( set ) { strip_cr = new_val; }

  return strip_cr;
}


/* if set, be backwards compatible */
auto traditional( bool set, bool new_val ) -> bool {
  static bool traditional = false;

  if( set ) { traditional = new_val; }

  return traditional;
}


static void show_help( const char * invocation_name ) {
  cout << "GNU ed is a line-oriented text editor. It is used to create, display,\n"
    "modify and otherwise manipulate text files, both interactively and via\n"
    "shell scripts. A restricted version of ed, red, can only edit files in\n"
    "the current directory and cannot execute shell commands. Ed is the\n"
    "'standard' text editor in the sense that it is the original editor for\n"
    "Unix, and thus widely available. For most purposes, however, it is\n"
    "superseded by full-screen editors such as GNU Emacs or GNU Moe.\n"
    "\nUsage: " << invocation_name << " [options] [file]\n";
  cout << "\nOptions:\n"
    "  -h, --help                 display this help and exit\n"
    "  -V, --version              output version information and exit\n"
    "  -E, --extended-regexp      use extended regular expressions\n"
    "  -G, --traditional          run in compatibility mode\n"
    "  -l, --loose-exit-status    exit with 0 status even if a command fails\n"
    "  -p, --prompt=STRING        use STRING as an interactive prompt\n"
    "  -r, --restricted           run in restricted mode\n"
    "  -s, --quiet, --silent      suppress diagnostics, byte counts and '!' prompt\n"
    "  -v, --verbose              be verbose; equivalent to the 'H' command\n"
    "      --strip-trailing-cr    strip carriage returns at end of text lines\n"
    "\nStart edit by reading in 'file' if given.\n"
    "If 'file' begins with a '!', read output of shell command.\n"
    "\nExit status: 0 for a normal exit, 1 for environmental problems (file\n"
    "not found, invalid flags, I/O errors, etc), 2 to indicate a corrupt or\n"
    "invalid input file, 3 for an internal consistency error (e.g., bug) which\n"
    "caused ed to panic.\n"
    "\nReport bugs to bug-ed@gnu.org\n"
    "Ed home page: http://www.gnu.org/software/ed/ed.html\n"
    "General help using GNU software: http://www.gnu.org/gethelp\n";
}


static void show_version( const char * program_name, const char * program_year ) {
  cout << "GNU " << program_name << " " << PROGVERSION << "\n";
  cout << "Copyright (C) 1994 Andrew L. Moore.\n"
    "Copyright (C) " << program_year << " Antonio Diaz Diaz.\n";
  cout << "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
    "This is free software: you are free to change and redistribute it.\n"
    "There is NO WARRANTY, to the extent permitted by law.\n";
}


void show_strerror( const char * const filename, const int errcode ) {
  auto filename_arr = span(filename, size_t(filename));

  if( !scripted() ) {
    if( (filename != nullptr) && (filename_arr[0] != 0) ) {
      cerr << filename << ": ";
    }
    cerr << strerror( errcode ) << "\n";
  }
}


static void show_error( const char * const msg, const int errcode,
			const bool help, const char * program_name,
			const char * invocation_name ) {
  auto msg_arr = span(msg, size_t(msg));

  if( (msg != nullptr) && (msg_arr[0] != 0) ) {
    cerr << program_name << ": " << msg
	 << ( ( errcode > 0 ) ? ": " : "" )
	 << ( ( errcode > 0 ) ? strerror( errcode ) : "" )
	 << "\n";
  }
  if( help ) {
    cerr << "Try '" << invocation_name << " --help' for more information.\n";
  }
}


/* return true if file descriptor is a regular file */
auto is_regular_file( const int fd ) -> bool {
  struct stat st {};
  return ( fstat( fd, &st ) != 0 || S_ISREG( st.st_mode ) );
}


auto may_access_filename( const char * const name ) -> bool {
  auto name_arr = span(name, size_t(name));

  if( restricted() ) {
    if( name_arr[0] == '!' )
      { set_error_msg( "Shell access restricted" ); return false; }
    if( strcmp( name, ".." ) == 0 || (strchr( name, '/' ) != nullptr) )
      { set_error_msg( "Directory access restricted" ); return false; }
  }
  return true;
}


auto main( const int argc, const char * const argv[] ) -> int {
  const char * const program_name = "ed";
  const char * const program_year = "2022";
  const char * invocation_name = "ed";		/* default value */

  auto argv_arr = span(argv, size_t(argv));

  int argind = 0;
  bool initial_error = false;		/* fatal error reading file */
  bool loose = false;
  enum { opt_cr = 256 };
  const struct ap_Option options[] =
    {
      { 'E', "extended-regexp",      ap_no  },
      { 'G', "traditional",          ap_no  },
      { 'h', "help",                 ap_no  },
      { 'l', "loose-exit-status",    ap_no  },
      { 'p', "prompt",               ap_yes },
      { 'r', "restricted",           ap_no  },
      { 's', "quiet",                ap_no  },
      { 's', "silent",               ap_no  },
      { 'v', "verbose",              ap_no  },
      { 'V', "version",              ap_no  },
      { opt_cr, "strip-trailing-cr", ap_no  },
      {  0, nullptr,                       ap_no } };

  struct Arg_parser parser {};
  if( argc > 0 ) {
    invocation_name = argv_arr[0];
  }

  if( ap_init( &parser, argc, argv, options, 0 ) == 0 )
    { show_error( "Memory exhausted.", 0, false, program_name, invocation_name ); return 1; }
  if( ap_error( &parser ) != nullptr )				/* bad option */
    { show_error( ap_error( &parser ), 0, true, program_name, invocation_name ); return 1; }

  for( argind = 0; argind < ap_arguments( &parser ); ++argind )
    {
      const int code = ap_code( &parser, argind );
      const char * const arg = ap_argument( &parser, argind );
      auto arg_arr = span(arg, size_t(arg));
      if( code == 0 ) {
	break;					/* no more options */
      }
      switch( code )
	{
	case 'E': extended_regexp(true, true); break;
	case 'G': traditional(true, true); break;	/* backward compatibility */
	case 'h': show_help( invocation_name ); return 0;
	case 'l': loose = true; break;
	case 'p': if( set_prompt( arg ) ) { break; } else { return 1; }
	case 'r': restricted(true, true); break;
	case 's': scripted(true, true); break;
	case 'v': set_verbose(); break;
	case 'V': show_version( program_name, program_year ); return 0;
	case opt_cr: strip_cr(true, true); break;
	default : show_error( "internal error: uncaught option.", 0, false, program_name, invocation_name );
	  return 3;
	}
    } /* end process options */

  setlocale( LC_ALL, "" );
  if( !init_buffers() ) { return 1; }

  while( argind < ap_arguments( &parser ) )
    {
      const char * const arg = ap_argument( &parser, argind );
      auto arg_arr = span(arg, size_t(arg));
      if( strcmp( arg, "-" ) == 0 ) { scripted(true, true); ++argind; continue; }
      if( may_access_filename( arg ) )
	{
	  const int ret = read_file( arg, 0 );
	  if( ret < 0 && is_regular_file( 0 ) ) { return 2; }
	  if( arg_arr[0] != '!' && !set_def_filename( arg ) ) { return 1; }
	  if( ret == -2 ) { initial_error = true; }
	}
      else
	{
	  if( is_regular_file( 0 ) ) { return 2; }
	  initial_error = true;
	}
      break;
    }
  ap_free( &parser );

  if( initial_error ) { fputs( "?\n", stdout ); }
  return main_loop( initial_error, loose );
}
