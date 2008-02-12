// zypper - a command line interface for libzypp
// http://en.opensuse.org/User:Mvidner

// (initially based on dmacvicar's zmart)

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <list>
#include <map>
#include <iterator>

#include <unistd.h>
#include <readline/history.h>

#include <boost/logic/tribool.hpp>
#include <boost/format.hpp>

#include "zypp/ZYppFactory.h"
#include "zypp/base/Logger.h"

#include "zypp/base/UserRequestException.h"
#include "zypp/repo/RepoException.h"
#include "zypp/zypp_detail/ZYppReadOnlyHack.h"

#include "zypp/target/rpm/RpmHeader.h" // for install <.rpmURI>

#include "zypper-main.h"
#include "zypper.h"
#include "zypper-repos.h"
#include "zypper-misc.h"

#include "zypper-tabulator.h"
#include "zypper-search.h"
#include "zypper-info.h"
#include "zypper-getopt.h"
#include "zypper-command.h"
#include "zypper-utils.h"
#include "zypper-callbacks.h"

using namespace std;
using namespace zypp;
//using namespace zypp::detail;
using namespace boost;

ZYpp::Ptr God = NULL;
RuntimeData gData;
parsed_opts copts; // command options

IMPL_PTR_TYPE(Zypper);

Zypper::Zypper()
  : _argc(0), _argv(NULL),
    _command(ZypperCommand::NONE),
    _exit_code(ZYPPER_EXIT_OK),
    _running_shell(false), _running_help(false),
    _sh_argc(0), _sh_argv(NULL)
{
  MIL << "Hi, me zypper " VERSION " built " << __DATE__ << " " <<  __TIME__ << endl;
}


Zypper::~Zypper()
{
  MIL << "Bye!" << endl;
}
 
Zypper_Ptr Zypper::instance()
{
  static Zypper_Ptr _instance;
  
  if (!_instance)
    _instance = new Zypper();

  return _instance;
}


int Zypper::main(int argc, char ** argv)
{
  _argc = argc;
  _argv = argv;

  // parse global options and the command
  try { processGlobalOptions(); }
  catch (const ExitRequestException & e)
  {
    MIL << "Caught exit request:" << endl << e.msg() << endl;
    return exitCode();
  }
  
  if (runningHelp())
  {
    safeDoCommand();
    return exitCode();
  }

  switch(command().toEnum())
  {
  case ZypperCommand::SHELL_e:
    commandShell();
    cleanup();
    return exitCode();

  case ZypperCommand::NONE_e:
    return ZYPPER_EXIT_ERR_SYNTAX;

  default:
    safeDoCommand();
    cleanup();
    return exitCode();
  }

  WAR << "This line should never be reached." << endl;

  return exitCode();
}

void print_main_help(const Zypper & zypper)
{
  static string help_global_options = _("  Global Options:\n"
    "\t--help, -h\t\tHelp.\n"
    "\t--version, -V\t\tOutput the version number.\n"
    "\t--quiet, -q\t\tSuppress normal output, print only error messages.\n"
    "\t--verbose, -v\t\tIncrease verbosity.\n"
    "\t--terse, -t\t\tTerse output for machine consumption.\n"
    "\t--table-style, -s\tTable style (integer).\n"
    "\t--rug-compatible, -r\tTurn on rug compatibility.\n"
    "\t--non-interactive, -n\tDon't ask anything, use default answers automatically.\n"
    "\t--reposd-dir, -D <dir>\tUse alternative repository definition files directory.\n"
    "\t--cache-dir, -C <dir>\tUse alternative meta-data cache database directory.\n"
    "\t--raw-cache-dir <dir>\tUse alternative raw meta-data cache directory\n"
  );
  
  static string help_global_source_options = _(
    "\tRepository Options:\n"
    "\t--no-gpg-checks\t\tIgnore GPG check failures and continue.\n"
    "\t--plus-repo, -p <URI>\tUse an additional repository\n"
    "\t--disable-repositories\tDo not read meta-data from repositories.\n"
    "\t--no-refresh\tDo not refresh the repositories.\n"
  );

  static string help_global_target_options = _("\tTarget Options:\n"
    "\t--root, -R <dir>\tOperate on a different root directory.\n"
    "\t--disable-system-resolvables  Do not read installed resolvables\n"
  );

  static string help_commands = _(
    "  Commands:\n"
    "\thelp, ?\t\t\tHelp\n"
    "\tshell, sh\t\tAccept multiple commands at once\n"
    "\tinstall, in\t\tInstall packages or resolvables\n"
    "\tremove, rm\t\tRemove packages or resolvables\n"
    "\tsearch, se\t\tSearch for packages matching a pattern\n"
    "\trepos, lr\t\tList all defined repositories.\n"
    "\taddrepo, ar\t\tAdd a new repository\n"
    "\tremoverepo, rr\t\tRemove specified repository\n"
    "\trenamerepo, nr\t\tRename specified repository\n"
    "\tmodifyrepo, mr\t\tModify specified repository\n"
    "\trefresh, ref\t\tRefresh all repositories\n"
    "\tpatch-check, pchk\tCheck for patches\n"
    "\tpatches, pch\t\tList patches\n"
    "\tlist-updates, lu\tList updates\n"
    "\txml-updates, xu\t\tList updates and patches in xml format\n"
    "\tupdate, up\t\tUpdate installed resolvables with newer versions.\n"
    "\tdist-upgrade, dup\tPerform a distribution upgrade\n"
    "\tinfo, if\t\tShow full information for packages\n"
    "\tpatch-info\t\tShow full information for patches\n"
    "\tsource-install, si\tInstall a source package\n"
    "");
  
  static string help_usage = _(
    "  Usage:\n"
    "\tzypper [--global-options] <command> [--command-options] [arguments]\n"
  );

  cout
    << help_usage << endl
    << help_global_options << endl
    << help_global_source_options << endl
    << help_global_target_options << endl
    << help_commands << endl;

  print_command_help_hint(zypper);
}

void print_unknown_command_hint(const Zypper & zypper)
{
  // TranslatorExplanation %s is "help" or "zypper help" depending on whether
  // zypper shell is running or not
  cout << format(_("Type '%s' to get a list of global options and commands."))
    % (zypper.runningShell() ? "help" : "zypper help") << endl;
}

void print_command_help_hint(const Zypper & zypper)
{
  // TranslatorExplanation %s is "help" or "zypper help" depending on whether
  // zypper shell is running or not
  cout << format(_("Type '%s' to get a command-specific help."))
    % (zypper.runningShell() ? "help <command>" : "zypper help <command>") << endl;
}

/*
 * parses global options, returns the command
 * 
 * \returns ZypperCommand object representing the command or ZypperCommand::NONE
 *          if an unknown command has been given. 
 */
void Zypper::processGlobalOptions()
{
  MIL << "START" << endl;

  static struct option global_options[] = {
    {"help",                       no_argument,       0, 'h'},
    {"verbose",                    no_argument,       0, 'v'},
    {"quiet",                      no_argument,       0, 'q'},
    {"version",                    no_argument,       0, 'V'},
    {"terse",                      no_argument,       0, 't'},
    {"table-style",                required_argument, 0, 's'},
    {"rug-compatible",             no_argument,       0, 'r'},
    {"non-interactive",            no_argument,       0, 'n'},
    {"no-gpg-checks",              no_argument,       0,  0 },
    {"root",                       required_argument, 0, 'R'},
    {"reposd-dir",                 required_argument, 0, 'D'},
    {"cache-dir",                  required_argument, 0, 'C'},
    {"raw-cache-dir",              required_argument, 0,  0 },
    {"opt",                        optional_argument, 0, 'o'},
    // TARGET OPTIONS
    {"disable-system-resolvables", no_argument,       0,  0 },
    // REPO OPTIONS
    {"plus-repo",                  required_argument, 0, 'p'},
    {"disable-repositories",       no_argument,       0,  0 },
    {"no-refresh",                 no_argument,       0,  0 },
    {0, 0, 0, 0}
  };

  // parse global options
  parsed_opts gopts = parse_options (_argc, _argv, global_options);
  if (gopts.count("_unknown"))
  {
    setExitCode(ZYPPER_EXIT_ERR_SYNTAX);
    return;
  }

  parsed_opts::const_iterator it;

  if (gopts.count("rug-compatible"))
    _gopts.is_rug_compatible = true;

  // Help is parsed by setting the help flag for a command, which may be empty
  // $0 -h,--help
  // $0 command -h,--help
  // The help command is eaten and transformed to the help option
  // $0 help
  // $0 help command
  if (gopts.count("help"))
    setRunningHelp(true);

  if (gopts.count("quiet")) {
    _gopts.verbosity = -1;
    DBG << "Verbosity " << _gopts.verbosity << endl;
  }

  if ((it = gopts.find("verbose")) != gopts.end()) {
    _gopts.verbosity += it->second.size(); 

//    _gopts.verbosity += gopts["verbose"].size();
    cout << format(_("Verbosity: %d")) % _gopts.verbosity << endl;
    DBG << "Verbosity " << _gopts.verbosity << endl;
  }

  if (gopts.count("non-interactive")) {
    _gopts.non_interactive = true;
    cout_n << _("Entering non-interactive mode.") << endl;
    MIL << "Entering non-interactive mode" << endl;
  }

  if (gopts.count("no-gpg-checks")) {
    _gopts.no_gpg_checks = true;
    cout_n << _("Entering no-gpg-checks mode.") << endl;
    MIL << "Entering no-gpg-checks mode" << endl;
  }

  if ((it = gopts.find("table-style")) != gopts.end()) {
    unsigned s;
    str::strtonum (it->second.front(), s);
    if (s < _End)
      Table::defaultStyle = (TableStyle) s;
    else
      cerr << _("Invalid table style ") << s << endl;
  }

  if ((it = gopts.find("root")) != gopts.end()) {
    _gopts.root_dir = it->second.front();
    Pathname tmp(_gopts.root_dir);
    if (!tmp.absolute())
    {
      cerr << _("The path specified in the --root option must be absolute.") << endl;
      _exit_code = ZYPPER_EXIT_ERR_INVALID_ARGS;
      return;
    }

    DBG << "root dir = " << _gopts.root_dir << endl;
    _gopts.rm_options.knownReposPath = _gopts.root_dir
      + _gopts.rm_options.knownReposPath;
    _gopts.rm_options.repoCachePath = _gopts.root_dir
      + _gopts.rm_options.repoCachePath;
    _gopts.rm_options.repoRawCachePath = _gopts.root_dir
      + _gopts.rm_options.repoRawCachePath;
  }

  if ((it = gopts.find("reposd-dir")) != gopts.end()) {
    _gopts.rm_options.knownReposPath = it->second.front();
  }

  if ((it = gopts.find("cache-dir")) != gopts.end()) {
    _gopts.rm_options.repoCachePath = it->second.front();
  }

  if ((it = gopts.find("raw-cache-dir")) != gopts.end()) {
    _gopts.rm_options.repoRawCachePath = it->second.front();
  }

  DBG << "repos.d dir = " << _gopts.rm_options.knownReposPath << endl;
  DBG << "cache dir = " << _gopts.rm_options.repoCachePath << endl;
  DBG << "raw cache dir = " << _gopts.rm_options.repoRawCachePath << endl;

  if (gopts.count("terse")) 
  {
    _gopts.machine_readable = true;
    cout << "<?xml version='1.0'?>" << endl;
    cout << "<stream>" << endl;
  }

  if (gopts.count("disable-repositories"))
  {
    MIL << "Repositories disabled, using target only." << endl;
    cout <<
        _("Repositories disabled, using the database of installed packages only.")
        << endl;
    _gopts.disable_system_sources = true;
  }
  else
  {
    MIL << "Repositories enabled" << endl;
  }

  if (gopts.count("no-refresh"))
  {
    _gopts.no_refresh = true;
    cout_v << _("Autorefresh disabled.") << endl;
    MIL << "Autorefresh disabled." << endl;
  }

  if (gopts.count("disable-system-resolvables"))
  {
    MIL << "System resolvables disabled" << endl;
    cout << _("Ignoring installed resolvables.") << endl;
    _gopts.disable_system_resolvables = true;
  }

  // testing option
  if ((it = gopts.find("opt")) != gopts.end()) {
    cout << "Opt arg: ";
    std::copy (it->second.begin(), it->second.end(),
               ostream_iterator<string> (cout, ", "));
    cout << endl;
  }

  // get command
  if (optind < _argc)
  {
    try { setCommand(ZypperCommand(_argv[optind++])); }
    // exception from command parsing
    catch (Exception & e)
    {
      cerr << e.asUserString() << endl;
      //setCommand(ZypperCommand::NONE);
    }
  }
  else if (!gopts.count("version"))
    setRunningHelp();

  if (command() == ZypperCommand::HELP)
  {
    setRunningHelp(true);
    if (optind < _argc)
    {
      string arg = _argv[optind++];
      try { setCommand(ZypperCommand(arg)); }
      catch (Exception e)
      {
        if (!arg.empty() && arg != "-h" && arg != "--help")
        {
          cout << e.asUserString() << endl;
          print_unknown_command_hint(*this);
          ZYPP_THROW(ExitRequestException("help provided"));
        }
      }
    }
    else
    {
      print_main_help(*this);
      ZYPP_THROW(ExitRequestException("help provided"));
    }
  }
  else if (command() == ZypperCommand::NONE)
  {
    if (runningHelp())
      print_main_help(*this);
    else if (gopts.count("version"))
      cout << PACKAGE " " VERSION << endl;
    else
    {
      print_unknown_command_hint(*this);
      setExitCode(ZYPPER_EXIT_ERR_SYNTAX);
    }
  }
  else if (command() == ZypperCommand::SHELL && optind < _argc)
  {
    string arg = _argv[optind++];
    if (!arg.empty())
    {
      if (arg == "-h" || arg == "--help")
        setRunningHelp(true);
      else
      {
        report_too_many_arguments("shell\n");
        setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
        ZYPP_THROW(ExitRequestException("help provided"));
      }
    }
  }

  // additional repositories
  if (gopts.count("plus-repo"))
  {
    if (command() == ZypperCommand::ADD_REPO ||
        command() == ZypperCommand::REMOVE_REPO ||
        command() == ZypperCommand::MODIFY_REPO ||
        command() == ZypperCommand::RENAME_REPO ||
        command() == ZypperCommand::REFRESH)
    {
      // TranslatorExplanation The %s is "--plus-repo"
      cout << format(_("The %s option has no effect here, ignoring."))
          % "--plus-repo" << endl;
    }
    else
    {
      list<string> repos = gopts["plus-repo"];

      int count = 1;
      for (list<string>::const_iterator it = repos.begin();
          it != repos.end(); ++it)
      {
        Url url = make_url (*it);
        if (!url.isValid())
        {
          setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
          return;
        }

        RepoInfo repo;
        repo.addBaseUrl(url);
        repo.setEnabled(true);
        repo.setAutorefresh(true);
        repo.setAlias(boost::str(format("tmp%d") % count));
        repo.setName(url.asString());

        gData.additional_repos.push_back(repo);
        DBG << "got additional repo: " << url << endl;
        count++;
      }
    }
  }

  MIL << "DONE" << endl;
}


void Zypper::commandShell()
{
  MIL << "Entering the shell" << endl;

  setRunningShell(true);

  string histfile;
  try {
    Pathname p (getenv ("HOME"));
    p /= ".zypper_history";
    histfile = p.asString ();
  } catch (...) {
    // no history
  }
  using_history ();
  if (!histfile.empty ())
    read_history (histfile.c_str ());

  while (true) {
    // read a line
    string line = readline_getline ();
    cerr_vv << "Got: " << line << endl;
    // reset optind etc
    optind = 0;
    // split it up and create sh_argc, sh_argv
    Args args(line);
    _sh_argc = args.argc();
    _sh_argv = args.argv();

    string command_str = _sh_argv[0] ? _sh_argv[0] : "";

    if (command_str == "\004") // ^D
    {
      cout << endl; // print newline after ^D
      break;
    }

    try
    {
      setCommand(ZypperCommand(command_str));
      if (command() == ZypperCommand::SHELL_QUIT)
        break;
      else if (command() == ZypperCommand::NONE)
        print_unknown_command_hint(*this);
      else
        safeDoCommand();
    }
    catch (Exception & e)
    {
      cerr << e.msg() <<  endl;
      print_unknown_command_hint(*this);
    }

    shellCleanup();
  }

  if (!histfile.empty ())
    write_history (histfile.c_str ());

  MIL << "Leaving the shell" << endl;
  setRunningShell(false);
}

void Zypper::shellCleanup()
{
  MIL << "Cleaning up for the next command." << endl;

  switch(command().toEnum())
  {
  case ZypperCommand::INSTALL_e:
  case ZypperCommand::REMOVE_e:
  //case Zyppercommand::UPDATE_e: TODO once the update will take arguments
  {
    remove_selections(*this);
    break;
  }
  default:;
  }

  // clear any previous arguments 
  _arguments.clear();
  // clear command options
  if (!_copts.empty())
  {
    _copts.clear();
    _cmdopts = CommandOptions();
  }
  // clear the command
  _command = ZypperCommand::NONE;
  // clear command help text
  _command_help.clear();
  // reset help flag
  setRunningHelp(false);
  // ... and the exit code
  setExitCode(ZYPPER_EXIT_OK);

  // gData
  gData.current_repo = RepoInfo();

  // TODO:
  // gData.repos re-read after repo operations or modify/remove these very repoinfos 
  // gData.repo_resolvables re-read only after certain repo operations (all?)
  // gData.target_resolvables re-read only after installation/removal/update
  // call target commit refresh pool after installation/removal/update (#328855)
}


/// process one command from the OS shell or the zypper shell
// catch unexpected exceptions and tell the user to report a bug (#224216)
void Zypper::safeDoCommand()
{
  try
  {
    processCommandOptions();
    if (command() == ZypperCommand::NONE || exitCode())
      return;
    doCommand();
  }
  catch (const AbortRequestException & ex)
  {
    ZYPP_CAUGHT(ex);

    //cerr << _("User requested to abort.") << endl;
    cerr << ex.asUserString() << endl;
  }
  catch (const ExitRequestException & e)
  {
    MIL << "Caught exit request:" << endl << e.msg() << endl; 
  }
  catch (const Exception & ex)
  {
    ZYPP_CAUGHT(ex);

    cerr << _("Unexpected exception.") << endl;
    cerr << ex.asUserString() << endl;
        report_a_bug(cerr);
  }

  if ( globalOpts().machine_readable )
    cout << "</stream>" << endl;
}

// === command-specific options ===
void Zypper::processCommandOptions()
{
  MIL << "START" << endl;

  struct option no_options = {0, 0, 0, 0};
  struct option *specific_options = &no_options;

  // this could be done in the processGlobalOptions() if there was no
  // zypper shell
  if (command() == ZypperCommand::HELP)
  {
    if (argc() > 1)
    {
      string cmd = argv()[1];
      try
      {
        setRunningHelp(true);
        setCommand(ZypperCommand(cmd));
      }
      catch (Exception & ex) {
        if (!cmd.empty() && cmd != "-h" && cmd != "--help")
        {
          cout << ex.asUserString() << endl;
          print_unknown_command_hint(*this);
          return;
        }
      }
    }
    else
    {
      print_main_help(*this);
      return;
    }
  }

  switch (command().toEnum())
  {

  // print help on help
  case ZypperCommand::HELP_e:
  {
    print_unknown_command_hint(*this);
    print_command_help_hint(*this);
    break;
  }

  case ZypperCommand::INSTALL_e:
  {
    static struct option install_options[] = {
      {"repo",                      required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog",                   required_argument, 0, 'c'},
      {"type",                      required_argument, 0, 't'},
      // the default (ignored)
      {"name",                      no_argument,       0, 'n'},
      {"force",                     no_argument,       0, 'f'},
      {"capability",                no_argument,       0, 'C'},
      // rug compatibility, we have global --non-interactive
      {"no-confirm",                no_argument,       0, 'y'},
      {"auto-agree-with-licenses",  no_argument,       0, 'l'},
      // rug compatibility, we have --auto-agree-with-licenses
      {"agree-to-third-party-licenses",  no_argument,  0, 0},
      {"debug-solver",              no_argument,       0, 0},
      {"force-resolution",          required_argument, 0, 'R'},
      {"dry-run",                   no_argument,       0, 'D'},
      {"help",                      no_argument,       0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = install_options;
    _command_help = boost::str(format(_(
      // TranslatorExplanation the first %s = "package, patch, pattern, product"
      //  and the second %s = "package"
      "install (in) [options] <capability|rpm_file_uri> ...\n"
      "\n"
      "Install resolvables with specified capabilities or RPM files with"
      " specified location. A capability is"
      " NAME[OP<VERSION>], where OP is one of <, <=, =, >=, >.\n"
      "\n"
      "  Command options:\n"
      "-r, --repo <alias|#|URI>        Install resolvables only from repository specified by alias.\n"
      "-t, --type <type>               Type of resolvable (%s)\n"
      "                                Default: %s\n"
      "-n, --name                      Select resolvables by plain name, not by capability\n"
      "-C, --capability                Select resolvables by capability\n"
      "-f, --force                     Install even if the item is already installed (reinstall)\n"
      "-l, --auto-agree-with-licenses  Automatically say 'yes' to third party license confirmation prompt.\n"
      "                                See 'man zypper' for more details.\n"
      "    --debug-solver              Create solver test case for debugging\n"
      "-R, --force-resolution <on|off> Force the solver to find a solution (even agressive)\n"
      "-D, --dry-run                   Test the installation, do not actually install\n"
    )) % "package, patch, pattern, product" % "package");
    break;
  }

  case ZypperCommand::REMOVE_e:
  {
    static struct option remove_options[] = {
      {"repo",       required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog",    required_argument, 0, 'c'},
      {"type",       required_argument, 0, 't'},
      // the default (ignored)
      {"name",       no_argument,       0, 'n'},
      {"capability", no_argument,       0, 'C'},
      // rug compatibility, we have global --non-interactive
      {"no-confirm", no_argument,       0, 'y'},
      {"debug-solver", no_argument,     0, 0},
      {"force-resolution", required_argument, 0, 'R'},
      {"dry-run",    no_argument,       0, 'D'},
      {"help",       no_argument,       0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = remove_options;
    _command_help = boost::str(format(_(
      // TranslatorExplanation the first %s = "package, patch, pattern, product"
      //  and the second %s = "package"
      "remove (rm) [options] <capability> ...\n"
      "\n"
      "Remove resolvables with specified capabilities. A capability is"
      " NAME[OP<VERSION>], where OP is one of <, <=, =, >=, >.\n"
      "\n"
      "  Command options:\n"
      "-r, --repo <alias|#|URI>        Operate only with resolvables from repository specified by alias.\n"
      "-t, --type <type>               Type of resolvable (%s)\n"
      "                                Default: %s\n"
      "-n, --name                      Select resolvables by plain name, not by capability\n"
      "-C, --capability                Select resolvables by capability\n"
      "    --debug-solver              Create solver test case for debugging\n"  
      "-R, --force-resolution <on|off> Force the solver to find a solution (even agressive)\n"
      "-D, --dry-run                   Test the removal, do not actually remove\n"
    )) % "package, patch, pattern, product" % "package");
    break;
  }
  
  case ZypperCommand::SRC_INSTALL_e:
  {
    static struct option src_install_options[] = {
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = src_install_options;
    _command_help = _(
      "source-install (si) <name> ...\n"
      "\n"
      "Install source packages specified by their names.\n"
      "\n"
      "This command has no additional options.\n"
    );
    break;
  }

  case ZypperCommand::ADD_REPO_e:
  {
    static struct option service_add_options[] = {
      {"type", required_argument, 0, 't'},
      {"disabled", no_argument, 0, 'd'},
      {"no-refresh", no_argument, 0, 'n'},
      {"repo", required_argument, 0, 'r'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = service_add_options;
    _command_help = boost::str(format(_(
      // TranslatorExplanation the %s = "yast2, rpm-md, plaindir"
      "addrepo (ar) [options] <URI> <alias>\n"
      "\n"
      "Add repository specified by URI to the system and assing the specified alias to it.\n"
      "\n"
      "  Command options:\n"
      "-r, --repo <FILE.repo>  Read the URL and alias from a file (even remote)\n"
      "-t, --type <TYPE>       Type of repository (%s)\n"
      "-d, --disabled          Add the repository as disabled\n"
      "-n, --no-refresh        Add the repository with auto-refresh disabled\n"
    )) % "yast2, rpm-md, plaindir");
    break;
  }

  case ZypperCommand::LIST_REPOS_e:
  {
    static struct option service_list_options[] = {
      {"export", required_argument, 0, 'e'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = service_list_options;
    _command_help = _(
      "repos (lr)\n"
      "\n"
      "List all defined repositories.\n"
      "\n"
      "  Command options:\n"
      "-e, --export <FILE.repo>  Export all defined repositories as a single local .repo file\n"
    );
    break;
  }

  case ZypperCommand::REMOVE_REPO_e:
  {
    static struct option service_delete_options[] = {
      {"help", no_argument, 0, 'h'},
      {"loose-auth", no_argument, 0, 0},
      {"loose-query", no_argument, 0, 0},
      {0, 0, 0, 0}
    };
    specific_options = service_delete_options;
    _command_help = _(
      "removerepo (rr) [options] <alias|URL>\n"
      "\n"
      "Remove repository specified by alias or URL.\n"
      "\n"
      "  Command options:\n"
      "    --loose-auth\tIgnore user authentication data in the URL\n"
      "    --loose-query\tIgnore query string in the URL\n"
    );
    break;
  }

  case ZypperCommand::RENAME_REPO_e:
  {
    static struct option service_rename_options[] = {
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = service_rename_options;
    _command_help = _(
      "renamerepo [options] <alias> <new-alias>\n"
      "\n"
      "Assign new alias to the repository specified by alias.\n"
      "\n"
      "This command has no additional options.\n"
    );
    break;
  }

  case ZypperCommand::MODIFY_REPO_e:
  {
    static struct option service_modify_options[] = {
      {"help", no_argument, 0, 'h'},
      {"disable", no_argument, 0, 'd'},
      {"enable", no_argument, 0, 'e'},
      {"enable-autorefresh", no_argument, 0, 'a'},
      {"disable-autorefresh", no_argument, 0, 0},
      {0, 0, 0, 0}
    };
    specific_options = service_modify_options;
    _command_help = _(
      "modifyrepo (mr) <options> <alias>\n"
      "\n"
      "Modify properties of the repository specified by alias.\n"
      "\n"
      "  Command options:\n"
      "-d, --disable             Disable the repository (but don't remove it)\n"
      "-e, --enable              Enable a disabled repository\n"
      "-a, --enable-autorefresh  Enable auto-refresh of the repository\n"
      "    --disable-autorefresh Disable auto-refresh of the repository\n"
    );
    break;
  }

  case ZypperCommand::REFRESH_e:
  {
    static struct option refresh_options[] = {
      {"force", no_argument, 0, 'f'},
      {"force-build", no_argument, 0, 'b'},
      {"force-download", no_argument, 0, 'd'},
      {"build-only", no_argument, 0, 'B'},
      {"download-only", no_argument, 0, 'D'},
      {"repo", required_argument, 0, 'r'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = refresh_options;
    _command_help = _(
      "refresh (ref) [alias|#] ...\n"
      "\n"
      "Refresh repositories specified by their alias or number."
      " If none are specified, all enabled repositories will be refreshed.\n"
      "\n"
      "  Command options:\n"
      "-f, --force              Force a complete refresh\n"
      "-b, --force-build        Force rebuild of the database\n"
      "-d, --force-download     Force download of raw metadata\n"
      "-B, --build-only         Only build the database, don't download metadata.\n"
      "-D, --download-only      Only download raw metadata, don't build the database\n"
      "-r, --repo <alias|#|URI> Refresh only specified repositories.\n"
    );
    break;
  }

  case ZypperCommand::LIST_UPDATES_e:
  {
    static struct option list_updates_options[] = {
      {"repo",        required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog",     required_argument, 0, 'c'},
      {"type",        required_argument, 0, 't'},
      {"best-effort", no_argument, 0, 0 },
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = list_updates_options;
    _command_help = boost::str(format(_(
      // TranslatorExplanation the first %s = "package, patch, pattern, product"
      //  and the second %s = "patch"
      "list-updates [options]\n"
      "\n"
      "List all available updates\n"
      "\n"
      "  Command options:\n"
      "-t, --type <type>         Type of resolvable (%s)\n"
      "                          Default: %s\n"
      "-r, --repo <alias|#|URI>  List only updates from the repository specified by the alias.\n"
      "    --best-effort         Do a 'best effort' approach to update, updates to\n"
      "                          a lower than latest-and-greatest version are\n"
      "                          also acceptable.\n"
    )) % "package, patch, pattern, product" % "patch");
    break;
  }

  case ZypperCommand::UPDATE_e:
  {
    static struct option update_options[] = {
      {"repo",                      required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog",                   required_argument, 0, 'c'},
      {"type",                      required_argument, 0, 't'},
      // rug compatibility option, we have global --non-interactive
      // note: rug used this uption only to auto-answer the 'continue with install?' prompt.
      {"no-confirm",                no_argument,       0, 'y'},
      {"skip-interactive",          no_argument,       0, 0},
      {"auto-agree-with-licenses",  no_argument,       0, 'l'},
      // rug compatibility, we have --auto-agree-with-licenses
      {"agree-to-third-party-licenses",  no_argument,  0, 0},
      {"best-effort",               no_argument,       0, 0},
      {"debug-solver",              no_argument,       0, 0},
      {"force-resolution",          required_argument, 0, 'R'},
      {"dry-run",                   no_argument,       0, 'D'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = update_options;
    _command_help = boost::str(format(_(
      // TranslatorExplanation the first %s = "package, patch, pattern, product"
      //  and the second %s = "patch"
      "update (up) [options]\n"
      "\n"
      "Update all installed resolvables with newer versions, where applicable.\n"
      "\n"
      "  Command options:\n"
      "\n"
      "-t, --type <type>               Type of resolvable (%s)\n"
      "                                Default: %s\n"
      "-r, --repo <alias|#|URI>        Limit updates to the repository specified by the alias.\n"
      "    --skip-interactive          Skip interactive updates\n"
      "-l, --auto-agree-with-licenses  Automatically say 'yes' to third party license confirmation prompt.\n"
      "                                See man zypper for more details.\n"
      "    --best-effort               Do a 'best effort' approach to update, updates to a lower than latest-and-greatest version are also acceptable\n"
      "    --debug-solver              Create solver test case for debugging\n"
      "-R, --force-resolution <on|off> Force the solver to find a solution (even agressive)\n"
      "-D, --dry-run                   Test the update, do not actually update\n"
    )) % "package, patch, pattern, product" % "patch");
    break;
  }

  case ZypperCommand::DIST_UPGRADE_e:
  {
    static struct option dupdate_options[] = {
      {"repo",                      required_argument, 0, 'r'},
      {"auto-agree-with-licenses",  no_argument,       0, 'l'},
      {"debug-solver",              no_argument,       0, 0},
      {"dry-run",                   no_argument,       0, 'D'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = dupdate_options;
    _command_help = _(
      "dist-upgrade (dup) [options]\n"
      "\n"
      "Perform a distribution upgrade.\n"
      "\n"
      "  Command options:\n"
      "\n"
      "-r, --repo <alias|#|URI>        Limit the upgrade to the repository specified by the alias.\n"
      "-l, --auto-agree-with-licenses  Automatically say 'yes' to third party license confirmation prompt.\n"
      "                                See man zypper for more details.\n"
      "    --debug-solver              Create solver test case for debugging\n"
      "-D, --dry-run                   Test the upgrade, do not actually upgrade\n"
    );
    break;
  }

  case ZypperCommand::SEARCH_e:
  {
    static struct option search_options[] = {
      {"installed-only", no_argument, 0, 'i'},
      {"uninstalled-only", no_argument, 0, 'u'},
      {"match-all", no_argument, 0, 0},
      {"match-any", no_argument, 0, 0},
      {"match-substrings", no_argument, 0, 0},
      {"match-words", no_argument, 0, 0},
      {"match-exact", no_argument, 0, 0},
      {"search-descriptions", no_argument, 0, 'd'},
      {"case-sensitive", no_argument, 0, 'c'},
      {"type",    required_argument, 0, 't'},
      {"sort-by-name", no_argument, 0, 0},
      // rug compatibility option, we have --sort-by-repo
      {"sort-by-catalog", no_argument, 0, 0},
      {"sort-by-repo", no_argument, 0, 0},
      // rug compatibility option, we have --repo
      {"catalog", required_argument, 0, 'c'}, //! \todo fix conflicting 'c' short option
      {"repo", required_argument, 0, 'r'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = search_options;
    _command_help = _(
      // TranslatorExplanation don't translate the resolvable types
      // (see the install command comment) 
      "search [options] [querystring...]\n"
      "\n"
      "Search for packages matching given search strings\n"
      "\n"
      "  Command options:\n"
      "    --match-all            Search for a match with all search strings (default)\n"
      "    --match-any            Search for a match with any of the search strings\n"
      "    --match-substrings     Matches with search strings may be partial words (default)\n"
      "    --match-words          Matches with search strings may only be whole words\n"
      "    --match-exact          Searches for an exact package name\n"
      "-d, --search-descriptions  Search also in package summaries and descriptions.\n"
      "-c, --case-sensitive       Perform case-sensitive search.\n"
      "-i, --installed-only       Show only packages that are already installed.\n"
      "-u, --uninstalled-only     Show only packages that are not currently installed.\n"
      "-t, --type <type>          Search only for packages of the specified type.\n"
      "-r, --repo <alias>         Search only in the repository specified by the alias.\n"
      "    --sort-by-name         Sort packages by name (default).\n"
      "    --sort-by-repo         Sort packages by repository.\n"
      "\n"
      "* and ? wildcards can also be used within search strings.\n"
    );
    break;
  }

  case ZypperCommand::PATCH_CHECK_e:
  {
    static struct option patch_check_options[] = {
      {"repo", required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = patch_check_options;
    _command_help = _(
      "patch-check\n"
      "\n"
      "Check for available patches\n"
      "\n"
      "  Command options:\n"
      "\n"
      "-r, --repo <alias|#|URI>  Check for patches only in the repository specified by the alias.\n"
    );
    break;
  }

  case ZypperCommand::SHOW_PATCHES_e:
  {
    static struct option patches_options[] = {
      {"repo", required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = patches_options;
    _command_help = _(
      "patches\n"
      "\n"
      "List all available patches\n"
      "\n"
      "  Command options:\n"
      "\n"
      "-r, --repo <alias|#|URI>  Check for patches only in the repository specified by the alias.\n"
    );
    break;
  }

  case ZypperCommand::INFO_e:
  {
    static struct option info_options[] = {
      {"type", required_argument, 0, 't'},
      {"repo", required_argument, 0, 'r'},
      // rug compatibility option, we have --repo
      {"catalog", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = info_options;
    _command_help =
      string(
      _("info <name> ...\n"
        "\n"
        "Show full information for packages")) + "\n"
        "\n" +
      _("  Command options:") + "\n" +
      _("-r, --repo <alias|#|URI>  Work only with the repository specified by the alias.") + "\n" +
      boost::str(format(
      _("-t, --type <type>         Type of resolvable (%s)\n"
        "                          Default: %s"))
          % "package, patch, pattern, product" % "package") + "\n"; 

    break;
  }

  // rug compatibility command, we have zypper info [-t <res_type>]
  case ZypperCommand::RUG_PATCH_INFO_e:
  {
    static struct option patch_info_options[] = {
      {"catalog", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = patch_info_options;
    _command_help = _(
      "patch-info <patchname> ...\n"
      "\n"
      "Show detailed information for patches\n"
      "\n"
      "This is a rug compatibility alias for 'zypper info -t patch'\n"
    );
    break;
  }

  // rug compatibility command, we have zypper info [-t <res_type>]
  case ZypperCommand::RUG_PATTERN_INFO_e:
  {
    static struct option options[] = {
      {"catalog", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = options;
    _command_help = _(
      "pattern-info <pattern_name> ...\n"
      "\n"
      "Show detailed information for patterns\n"
      "\n"
      "This is a rug compatibility alias for 'zypper info -t pattern'\n"
    );
    break;
  }

  // rug compatibility command, we have zypper info [-t <res_type>]
  case ZypperCommand::RUG_PRODUCT_INFO_e:
  {
    static struct option options[] = {
      {"catalog", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = options;
    _command_help = _(
      "product-info <product_name> ...\n"
      "\n"
      "Show detailed information for products\n"
      "\n"
      "This is a rug compatibility alias for 'zypper info -t product'\n"
    );
    break;
  }

  case ZypperCommand::MOO_e:
  {
    static struct option moo_options[] = {
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = moo_options;
    _command_help = _(
      "moo\n"
      "\n"
      "Show an animal\n"
      "\n"
      "This command has no additional options.\n"
      );
    break;
  }

  case ZypperCommand::XML_LIST_UPDATES_PATCHES_e:
  {
    static struct option xml_updates_options[] = {
      {"repo", required_argument, 0, 'r'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = xml_updates_options;
    _command_help = _(
      "xml-updates\n"
      "\n"
      "Show updates and patches in xml format\n"
      "\n"
      "  Command options:\n"
      "-r, --repo <alias|#|URI>  Work only with updates from repository specified by alias.\n"
    );
    break;
  }

  case ZypperCommand::SHELL_QUIT_e:
  {
    static struct option quit_options[] = {
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = quit_options;
    _command_help = _(
      "quit (exit, ^D)\n"
      "\n"
      "Quit the current zypper shell.\n"
      "\n"
      "This command has no additional options.\n"
    );
    break;
  }

  case ZypperCommand::SHELL_e:
  {
    static struct option quit_options[] = {
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };
    specific_options = quit_options;
    _command_help = _(
      "shell\n"
      "\n"
      "Enter the zypper command shell.\n"
      "\n"
      "This command has no additional options.\n"
    );
    break;
  }

  default:
  {
    if (runningHelp())
      break;

    ERR << "Unknown or unexpected command" << endl;
    cerr << _("Unexpected program flow") << "." << endl;
    report_a_bug(cerr);
  }
  }

  // parse command options
  if (!runningHelp())
  {
    ::copts = _copts = parse_options (argc(), argv(), specific_options);
    if (copts.count("_unknown"))
    {
      setExitCode(ZYPPER_EXIT_ERR_SYNTAX);
      ERR << "Unknown option, returning." << endl;
      return;
    }

    MIL << "Done parsing options." << endl;

    // treat --help command option like global --help option from now on
    // i.e. when used together with command to print command specific help
    setRunningHelp(runningHelp() || copts.count("help"));

    if (optind < argc()) {
      cout_v << _("Non-option program arguments: ");
      while (optind < argc()) {
        string argument = argv()[optind++];
        cout_v << "'" << argument << "' ";
        _arguments.push_back (argument);
      }
      cout_v << endl;
    }

    // here come commands that need the lock
    try {
      if (command() == ZypperCommand::LIST_REPOS)
        zypp_readonly_hack::IWantIt (); // #247001, #302152
  
      God = zypp::getZYpp();
    }
    catch (Exception & excpt_r) {
      ZYPP_CAUGHT (excpt_r);
      ERR  << "A ZYpp transaction is already in progress." << endl;
      string msg = _("A ZYpp transaction is already in progress."
          " This means, there is another application using the libzypp library for"
          " package management running. All such applications must be closed before"
          " using this command.");
  
      if ( globalOpts().machine_readable )
        cout << "<message type=\"error\">" << msg  << "</message>" <<  endl;
      else
        cerr << msg << endl;

      setExitCode(ZYPPER_EXIT_ERR_ZYPP);
      throw (ExitRequestException("ZYpp locked"));
    }
  }

  MIL << "Done " << endl;
}

/// process one command from the OS shell or the zypper shell
void Zypper::doCommand()
{
  MIL << "Going to process command " << command().toEnum() << endl;
  ResObject::Kind kind;


  // === execute command ===

  // --------------------------( moo )----------------------------------------

  if (command() == ZypperCommand::MOO)
  {
    if (runningHelp()) { cout << _command_help << endl; return; }

    // TranslatorExplanation this is a hedgehog, paint another animal, if you want
    cout_n << _("   \\\\\\\\\\\n  \\\\\\\\\\\\\\__o\n__\\\\\\\\\\\\\\'/_") << endl;
    return;
  }

  // --------------------------( repo list )----------------------------------
  
  else if (command() == ZypperCommand::LIST_REPOS)
  {
    if (runningHelp()) { cout << _command_help << endl; return; }
    // if (runningHelp()) display_command_help()

    list_repos(*this);
    return;
  }

  // --------------------------( addrepo )------------------------------------
  
  else if (command() == ZypperCommand::ADD_REPO)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for modifying system repositories.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    // too many arguments
    if (_arguments.size() > 2)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    // indeterminate indicates the user has not specified the values
    tribool enabled(indeterminate); 
    tribool refresh(indeterminate);

    if (copts.count("disabled"))
      enabled = false;
    if (copts.count("no-refresh"))
      refresh = false;

    try
    {
      // add repository specified in .repo file
      if (copts.count("repo"))
      {
        add_repo_from_file(*this,copts["repo"].front(), enabled, refresh);
        return;
      }
  
      // force specific repository type. Validation is done in add_repo_by_url()
      string type = copts.count("type") ? copts["type"].front() : "";
  
      // display help message if insufficient info was given
      if (_arguments.size() < 2)
      {
        cerr << _("Too few arguments. At least URL and alias are required.") << endl;
        ERR << "Too few arguments. At least URL and alias are required." << endl;
        cout_n << _command_help;
        setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
        return;
      }

      Url url = make_url (_arguments[0]);
      if (!url.isValid())
      {
        setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
        return;
      }

      // by default, enable the repo and set autorefresh
      if (indeterminate(enabled)) enabled = true;
      if (indeterminate(refresh)) refresh = true;

      warn_if_zmd();

      // load gpg keys
      cond_init_target(*this);

      add_repo_by_url(
          *this, url, _arguments[1]/*alias*/, type, enabled, refresh);
    }
    catch (const repo::RepoUnknownTypeException & e)
    {
      ZYPP_CAUGHT(e);
      report_problem(e,
          _("Specified type is not a valid repository type:"),
          _("See 'zypper -h addrepo' or man zypper to get a list of known repository types."));
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
    }

    return;
  }

  // --------------------------( delete repo )--------------------------------

  else if (command() == ZypperCommand::REMOVE_REPO)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for modifying system repositories.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    if (_arguments.size() < 1)
    {
      cerr << _("Required argument missing.") << endl;
      ERR << "Required argument missing." << endl;
      cout_n << _("Usage") << ':' << endl;
      cout_n << _command_help;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    // too many arguments
    //! \todo allow to specify multiple repos to delete
    else if (_arguments.size() > 1)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    warn_if_zmd ();

    bool found = remove_repo(*this, _arguments[0]);
    if (found)
      return;

    MIL << "Repository not found by alias, trying delete by URL" << endl;
    cout_v << _("Repository not found by alias, trying delete by URL...") << endl;

    Url url;
    try { url = Url (_arguments[0]); }
    catch ( const Exception & excpt_r )
    {
      ZYPP_CAUGHT( excpt_r );
      cerr_v << _("Given URL is invalid.") << endl;
      cerr_v << excpt_r.asUserString() << endl;
    }

    if (url.isValid())
    {
      url::ViewOption urlview = url::ViewOption::DEFAULTS + url::ViewOption::WITH_PASSWORD;

      if (copts.count("loose-auth"))
      {
        urlview = urlview
          - url::ViewOptions::WITH_PASSWORD
          - url::ViewOptions::WITH_USERNAME;
      }

      if (copts.count("loose-query"))
        urlview = urlview - url::ViewOptions::WITH_QUERY_STR;

      found = remove_repo(*this, url, urlview);
    }
    else
      found = false;

    if (!found)
    {
      MIL << "Repository not found by given alias or URL." << endl;
      cerr << _("Repository not found by given alias or URL.") << endl;
    }

    return;
  }

  // --------------------------( rename repo )--------------------------------

  else if (command() == ZypperCommand::RENAME_REPO)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for modifying system repositories.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    if (_arguments.size() < 2)
    {
      cerr << _("Too few arguments. At least URL and alias are required.") << endl;
      ERR << "Too few arguments. At least URL and alias are required." << endl;
      cout_n << _command_help;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }
    // too many arguments
    else if (_arguments.size() > 2)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

//    cond_init_target(*this);
    warn_if_zmd ();
    try {
      // also stores it
      rename_repo(*this, _arguments[0], _arguments[1]);
    }
    catch ( const Exception & excpt_r )
    {
      cerr << excpt_r.asUserString() << endl;
      setExitCode(ZYPPER_EXIT_ERR_ZYPP);
      return;
    }

    return;
  }

  // --------------------------( modify repo )--------------------------------

  else if (command() == ZypperCommand::MODIFY_REPO)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for modifying system repositories.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    if (_arguments.size() < 1)
    {
      cerr << _("Alias is a required argument.") << endl;
      ERR << "Na alias argument given." << endl;
      cout_n << _command_help;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }
    // too many arguments
    if (_arguments.size() > 1)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    modify_repo(*this, _arguments[0]);
  }

  // --------------------------( refresh )------------------------------------

  else if (command() == ZypperCommand::REFRESH)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for refreshing system repositories.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }
    
    if (globalOpts().no_refresh)
      cout << format(_("The '%s' global option has no effect here."))
        % "--no-refresh" << endl;

    refresh_repos(*this);
    return;
  }

  // --------------------------( remove/install )-----------------------------

  else if (command() == ZypperCommand::INSTALL ||
           command() == ZypperCommand::REMOVE)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    if (_arguments.size() < 1)
    {
      cerr << _("Too few arguments.");
      cerr << " " << _("At least one package name is required.") << endl << endl;
      cerr << _command_help;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    if (copts.count("auto-agree-with-licenses")
        || copts.count("agree-to-third-party-licenses"))
      _cmdopts.license_auto_agree = true;

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for installing or uninstalling packages.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    // rug compatibility code
    // switch on non-interactive mode if no-confirm specified
    if (copts.count("no-confirm"))
      _gopts.non_interactive = true;


    // read resolvable type
    string skind = copts.count("type")?  copts["type"].front() : "package";
    kind = string_to_kind (skind);
    if (kind == ResObject::Kind ()) {
      cerr << format(_("Unknown resolvable type: %s")) % skind << endl;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    bool install_not_remove = command() == ZypperCommand::INSTALL;

    // check for rpm files among the arguments
    list<string> rpms_files_caps;
    if (install_not_remove)
    {
      for (vector<string>::iterator it = _arguments.begin();
            it != _arguments.end(); )
      {
        if (looks_like_rpm_file(*it))
        {
          DBG << *it << " looks like rpm file" << endl;
          cout_v << format(
              _("'%s' looks like an RPM file. Will try to download it.")) % *it
            << endl;

          // download the rpm into the cache
          //! \todo do we want this or a tmp dir? What about the files cached before?
          //! \todo optimize: don't mount the same media multiple times for each rpm
          Pathname rpmpath = cache_rpm(*it, 
              (_gopts.root_dir != "/" ? _gopts.root_dir : "")
              + ZYPPER_RPM_CACHE_DIR);

          if (rpmpath.empty())
          {
            cerr << format(_("Problem with the RPM file specified as '%s', skipping."))
              % *it << endl;
          }
          else
          {
            using target::rpm::RpmHeader;
            // rpm header (need name-version-release)
            RpmHeader::constPtr header =
              RpmHeader::readPackage(rpmpath, RpmHeader::NOSIGNATURE);
            if (header)
            {
              string nvrcap =
                header->tag_name() + "=" +
                header->tag_version() + "-" +
                header->tag_release(); 
              DBG << "rpm package capability: " << nvrcap << endl;
  
              // store the rpm file capability string (name=version-release) 
              rpms_files_caps.push_back(nvrcap);
            }
            else
            {
              cerr << format(
                _("Problem reading the RPM header of %s. Is it an RPM file?"))
                  % *it << endl;
            }
          }

          // remove this rpm argument 
          it = _arguments.erase(it);
        }
        else
          ++it;
      }
    }

    // if there were some rpm files, add the rpm cache as a temporary plaindir repo 
    if (!rpms_files_caps.empty())
    {
      // add a plaindir repo
      RepoInfo repo;
      repo.setType(repo::RepoType::RPMPLAINDIR);
      repo.addBaseUrl(Url("dir://"
          + (_gopts.root_dir != "/" ? _gopts.root_dir : "")
          + ZYPPER_RPM_CACHE_DIR));
      repo.setEnabled(true);
      repo.setAutorefresh(true);
      repo.setAlias("_tmpRPMcache_");
      repo.setName(_("RPM files cache"));

      gData.additional_repos.push_back(repo);
    }
    // no rpms and no other arguments either
    else if (_arguments.empty())
    {
      cout << _("No valid arguments specified.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    //! \todo quit here if the argument list remains empty after founding only invalid rpm args

    // prepare repositories
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;

    if ( gData.repos.empty() )
    {
      cerr << _("Warning: No repositories defined."
          " Operating only with the installed resolvables."
          " Nothing can be installed.") << endl;
    }

    // prepare target
    cond_init_target(*this);
    // load metadata
    cond_load_resolvables(*this);

    // mark resolvables for installation/removal
    bool by_capability = false; // install by name by default
    //! \todo install by capability by default in the future (need to improve its output)
    if (copts.count("capability"))
      by_capability = true;
    for ( vector<string>::const_iterator it = _arguments.begin();
          it != _arguments.end(); ++it )
    {
      if (by_capability)
        mark_by_capability (*this, install_not_remove, kind, *it);
      else
        mark_by_name (*this, install_not_remove, kind, *it);
    }

    // rpm files
    for ( list<string>::const_iterator it = rpms_files_caps.begin();
          it != rpms_files_caps.end(); ++it )
      mark_by_capability (*this, true, kind, *it);

    // solve dependencies
    if (copts.count("debug-solver"))
    {
      establish();
      cout_n << _("Generating solver test case...") << endl;
      if (God->resolver()->createSolverTestcase("/var/log/zypper.solverTestCase"))
        cout_n << _("Solver test case generated successfully.") << endl;
      else
      {
        cerr << _("Error creating the solver test case.") << endl;
        setExitCode(ZYPPER_EXIT_ERR_ZYPP);
        return;
      }
    }
    else
    {
      solve_and_commit(*this);
    }

    return;
  }

  // -------------------( source install )------------------------------------

  else if (command() == ZypperCommand::SRC_INSTALL)
  {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    if (_arguments.size() < 1)
    {
      cerr << _("Source package name is a required argument.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;

    cond_init_target(*this);
    // load only repo resolvables, we don't need the installed ones
    load_repo_resolvables(*this);

    setExitCode(source_install(_arguments));
    return;
  }

  // --------------------------( search )-------------------------------------

  else if (command() == ZypperCommand::SEARCH)
  {
    zypp::PoolQuery query;

    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    if (globalOpts().disable_system_resolvables || copts.count("uninstalled-only"))
      query.setUninstalledOnly();

    if (copts.count("installed-only")) query.setInstalledOnly();
    //if (copts.count("match-any")) options.setMatchAny();
    //if (copts.count("match-words")) options.setMatchWords();
    if (copts.count("match-exact")) query.setMatchExact();
    //if (copts.count("search-descriptions")) options.setSearchDescriptions();
    if (copts.count("case-sensitive")) query.setCaseSensitive();

    if (copts.count("type") > 0)
    {
      std::list<std::string>::const_iterator it;
      for (it = copts["type"].begin(); it != copts["type"].end(); ++it)
      {
        kind = string_to_kind( *it );
        if (kind == ResObject::Kind())
        {
          cerr << _("Unknown resolvable type ") << *it << endl;
          setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
          return;
        }
        query.addKind( kind );
      }
    }
    else if (globalOpts().is_rug_compatible)
    {
      query.addKind( ResTraits<Package>::kind );
    }

    if (copts.count("repo") > 0)
    {
      //options.clearRepos();
      std::list<std::string>::const_iterator it;
      for (it = copts["repo"].begin(); it != copts["repo"].end(); ++it) {
        query.addRepo( *it );
      }
    }

    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;

    cond_init_target(*this);

    // now load resolvables:
    cond_load_resolvables(*this);

    Table t;
    t.style(Ascii);

    try
    {
      
      FillTable callback( t, query );
      query.execute(_arguments[0], callback );
  
      if (t.empty())
        cout << _("No resolvables found.") << endl;
      else {
        cout << endl;
        if (copts.count("sort-by-catalog") || copts.count("sort-by-repo"))
          t.sort(1);
        else
          t.sort(3); // sort by name
        cout << t;
      }
    }
    catch (const Exception & e)
    {
      report_problem(e,
        _("Problem occurred initializing or executing the search query") + string(":"),
        string(_("See the above message for a hint.")) + " " +
          _("Running 'zypper refresh' as root might resolve the problem."));
      setExitCode(ZYPPER_EXIT_ERR_ZYPP);
    }

    return;
  }

  // --------------------------( patch check )--------------------------------

  // TODO: rug summary
  else if (command() == ZypperCommand::PATCH_CHECK) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // too many arguments
    if (_arguments.size() > 0)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    cond_init_target(*this);

    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;

    // TODO additional_sources
    // TODO warn_no_sources
    // TODO calc token?

    // now load resolvables:
    cond_load_resolvables(*this);

    establish ();
    patch_check ();

    if (gData.security_patches_count > 0)
    {
      setExitCode(ZYPPER_EXIT_INF_SEC_UPDATE_NEEDED);
      return;
    }
    if (gData.patches_count > 0)
    {
      setExitCode(ZYPPER_EXIT_INF_UPDATE_NEEDED);
      return;
    }

    return;
  }

  // --------------------------( patches )------------------------------------

  else if (command() == ZypperCommand::SHOW_PATCHES) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // too many arguments
    if (_arguments.size() > 0)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    cond_init_target(*this);
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;
    cond_load_resolvables(*this);
    establish();
    show_patches(*this);

    return;
  }

  // --------------------------( list updates )-------------------------------

  else if (command() == ZypperCommand::LIST_UPDATES) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // too many arguments
    if (_arguments.size() > 0)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    string skind = copts.count("type")?  copts["type"].front() :
      globalOpts().is_rug_compatible? "package" : "patch";
    kind = string_to_kind (skind);
    if (kind == ResObject::Kind ()) {
      cerr << format(_("Unknown resolvable type: %s")) % skind << endl;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    bool best_effort = copts.count( "best-effort" ); 

    if (globalOpts().is_rug_compatible && best_effort) {
	best_effort = false;
	// 'rug' is the name of a program and must not be translated
	// 'best-effort' is a program parameter and can not be translated
	cerr << _("Running as 'rug', can't do 'best-effort' approach to update.") << endl;
    }
    cond_init_target(*this);
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;
    cond_load_resolvables(*this);
    establish ();

    list_updates(*this, kind, best_effort );

    return;
  }


  // -----------------( xml list updates and patches )------------------------

  else if (command() == ZypperCommand::XML_LIST_UPDATES_PATCHES) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    cond_init_target(*this);
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;
    cond_load_resolvables(*this);
    establish ();

    cout << "<update-status version=\"0.6\">" << endl;
    cout << "<update-list>" << endl;
    if (!xml_list_patches ())	// Only list updates if no
      xml_list_updates ();	// affects-pkg-mgr patches are available
    cout << "</update-list>" << endl;
    cout << "</update-status>" << endl;

    return;
  }

  // -----------------------------( update )----------------------------------

  else if (command() == ZypperCommand::UPDATE) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for updating packages.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    // too many arguments
    if (_arguments.size() > 0)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    // rug compatibility code
    // switch on non-interactive mode if no-confirm specified
    if (copts.count("no-confirm"))
      _gopts.non_interactive = true;

    if (copts.count("auto-agree-with-licenses")
        || copts.count("agree-to-third-party-licenses"))
      _cmdopts.license_auto_agree = true;

    string skind = copts.count("type")?  copts["type"].front() :
      globalOpts().is_rug_compatible? "package" : "patch";
    kind = string_to_kind (skind);
    if (kind == ResObject::Kind ()) {
	cerr << format(_("Unknown resolvable type: %s")) % skind << endl;
	setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
	return;
    }

    bool best_effort = copts.count( "best-effort" ); 

    if (globalOpts().is_rug_compatible && best_effort) {
	best_effort = false;
	// 'rug' is the name of a program and must not be translated
	// 'best-effort' is a program parameter and can not be translated
	cerr << _("Running as 'rug', can't do 'best-effort' approach to update.") << endl;
    }
    cond_init_target(*this);
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;
    cond_load_resolvables(*this);
    establish ();

    bool skip_interactive = copts.count("skip-interactive") || globalOpts().non_interactive;
    mark_updates( kind, skip_interactive, best_effort );


    if (copts.count("debug-solver"))
    {
      cout_n << _("Generating solver test case...") << endl;
      if (God->resolver()->createSolverTestcase("/var/log/zypper.solverTestCase"))
        cout_n << _("Solver test case generated successfully.") << endl;
      else
        cerr << _("Error creating the solver test case.") << endl;
    }
    // commit
    // returns ZYPPER_EXIT_OK, ZYPPER_EXIT_ERR_ZYPP,
    // ZYPPER_EXIT_INF_REBOOT_NEEDED, or ZYPPER_EXIT_INF_RESTART_NEEDED
    else
    {
      solve_and_commit(*this);
    }

    return; 
  }

  // ----------------------------( dist-upgrade )------------------------------

  else if (command() == ZypperCommand::DIST_UPGRADE) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    // check root user
    if (geteuid() != 0)
    {
      cerr << _("Root privileges are required for performing a distribution upgrade.") << endl;
      setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
      return;
    }

    // too many arguments
    if (_arguments.size() > 0)
    {
      report_too_many_arguments(_command_help);
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    if (copts.count("auto-agree-with-licenses"))
      _cmdopts.license_auto_agree = true;

    cond_init_target(*this);
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;
    cond_load_resolvables(*this);
    establish ();
    zypp::UpgradeStatistics opt_stats;
    God->resolver()->doUpgrade(opt_stats);

    if (copts.count("debug-solver"))
    {
      cout_n << _("Generating solver test case...") << endl;
      if (God->resolver()->createSolverTestcase("/var/log/zypper.solverTestCase"))
        cout_n << _("Solver test case generated successfully.") << endl;
      else
        cerr << _("Error creating the solver test case.") << endl;
    }
    // commit
    // returns ZYPPER_EXIT_OK, ZYPPER_EXIT_ERR_ZYPP,
    // ZYPPER_EXIT_INF_REBOOT_NEEDED, or ZYPPER_EXIT_INF_RESTART_NEEDED
    else
    {
      solve_and_commit(*this);
    }

    return; 
  }

  // -----------------------------( info )------------------------------------

  else if (command() == ZypperCommand::INFO ||
           command() == ZypperCommand::RUG_PATCH_INFO ||
           command() == ZypperCommand::RUG_PATTERN_INFO ||
           command() == ZypperCommand::RUG_PRODUCT_INFO) {
    if (runningHelp())
    {
      cout << _command_help;
      return;
    }

    if (_arguments.size() < 1)
    {
      cerr << _("Required argument missing.") << endl;
      ERR << "Required argument missing." << endl;
      cout_n << _("Usage") << ':' << endl;
      cout_n << _command_help;
      setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      return;
    }

    switch (command().toEnum())
    {
    case ZypperCommand::RUG_PATCH_INFO_e:
      kind =  ResTraits<Patch>::kind;
      break;
    case ZypperCommand::RUG_PATTERN_INFO_e:
      kind =  ResTraits<Pattern>::kind;
      break;
    case ZypperCommand::RUG_PRODUCT_INFO_e:
      kind =  ResTraits<Product>::kind;
      break;
    default:
    case ZypperCommand::INFO_e:
      // read resolvable type
      string skind = copts.count("type")?  copts["type"].front() : "package";
      kind = string_to_kind (skind);
      if (kind == ResObject::Kind ()) {
        cerr << format(_("Unknown resolvable type: %s")) % skind << endl;
        setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
        return;
      }
    }

    cond_init_target(*this);
    init_repos(*this);
    if (exitCode() != ZYPPER_EXIT_OK)
      return;
    cond_load_resolvables(*this);
    establish ();

    printInfo(*this, kind);

    return;
  }

  else if (command() == ZypperCommand::SHELL_QUIT)
  {
    if (runningHelp())
      cout << _command_help;
    else if (!runningShell())
      cout << _("This command only makes sense in the zypper shell.") << endl;
    else
      cout << "oops, you wanted to quit, didn't you?" << endl;

    return;
  }

  else if (command() == ZypperCommand::SHELL)
  {
    if (runningHelp())
      cout << _command_help;
    else if (runningShell())
      cout << _("You already are running zypper's shell.") << endl;
    else
    {
      cerr << _("Unexpected program flow") << "." << endl;
      report_a_bug(cerr);
    }

    return;
  }

  // if the program reaches this line, something went wrong
  setExitCode(ZYPPER_EXIT_ERR_BUG);
}

void Zypper::cleanup()
{
  MIL << "START" << endl;

  // remove the additional repositories specified by --plus-repo
  for (list<RepoInfo>::const_iterator it = gData.additional_repos.begin();
         it != gData.additional_repos.end(); ++it)
    remove_repo(*this, *it);
}

// Local Variables:
// c-basic-offset: 2
// End:
