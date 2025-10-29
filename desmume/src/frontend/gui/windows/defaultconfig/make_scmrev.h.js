var wshShell		= new ActiveXObject("WScript.Shell")
var oFS				= new ActiveXObject("Scripting.FileSystemObject");

var outfile			= "./defaultconfig/scmrev.h";
var cmd_revision	= " rev-parse HEAD";
var cmd_describe	= " describe --always --long --dirty";
var cmd_branch		= " rev-parse --abbrev-ref HEAD";

function GetGitExe()
{
	try
	{
		gitexe = wshShell.RegRead("HKCU\\Software\\GitExtensions\\gitcommand");
		wshShell.Exec(gitexe);
		return gitexe;
	}
	catch (e)
	{}

	for (var gitexe in {"git.cmd":1, "git":1, "git.bat":1})
	{
		try
		{
			wshShell.Exec(gitexe);
			return gitexe;
		}
		catch (e)
		{}
	}

	// last try - msysgit not in path (vs2015 default)
	msyspath = "\\Git\\cmd\\git.exe";
	gitexe = wshShell.ExpandEnvironmentStrings("%PROGRAMFILES(x86)%") + msyspath;
	if (oFS.FileExists(gitexe)) {
		return gitexe;
	}
	gitexe = wshShell.ExpandEnvironmentStrings("%PROGRAMFILES%") + msyspath;
	if (oFS.FileExists(gitexe)) {
		return gitexe;
	}
	gitexe = wshShell.ExpandEnvironmentStrings("%ProgramW6432%") + msyspath;
	if (oFS.FileExists(gitexe)) {
		return gitexe;
	}	

	WScript.Echo("Cannot find git or git.cmd, check your PATH:\n" +
		wshShell.ExpandEnvironmentStrings("%PATH%"));
	return "";
}

function GetFirstStdOutLine(cmd)
{
	try
	{
		return wshShell.Exec(cmd).StdOut.ReadLine();
	}
	catch (e)
	{
		// catch "the system cannot find the file specified" error
		WScript.Echo("Failed to exec " + cmd + " this should never happen");
		WScript.Quit(1);
	}
}

function GetFileContents(f)
{
	try
	{
		return oFS.OpenTextFile(f).ReadAll();
	}
	catch (e)
	{
		// file doesn't exist
		return "";
	}
}

// get info from git
var revision = "SCM_REV_STR";
var describe = "SCM_DESC_STR";
var branch = "SCM_BRANCH_STR"
var isStable = "0"

var gitexe = GetGitExe();

if(gitexe != "")
{	
	revision	= GetFirstStdOutLine(gitexe + cmd_revision);
	describe	= GetFirstStdOutLine(gitexe + cmd_describe);
	branch		= GetFirstStdOutLine(gitexe + cmd_branch);
	isStable	= +("master" == branch || "stable" == branch);
}

// remove hash (and trailing "-0" if needed) from description
describe = describe.replace(/(-0)?-[^-]+(-dirty)?$/, '$2');

var out_contents =
	"#define SCM_REV_STR \"" + revision + "\"\n" +
	"#define SCM_DESC_STR \"" + describe + "\"\n" +
	"#define SCM_BRANCH_STR \"" + branch + "\"\n" +
	"#define SCM_IS_MASTER " + isStable + "\n";

// check if file needs updating
if (out_contents == GetFileContents(outfile))
{
	WScript.Echo(outfile + " current at " + describe);
}
else
{
	// needs updating - writeout current info
	oFS.CreateTextFile(outfile, true).Write(out_contents);
	WScript.Echo(outfile + " updated to " + describe);
}
