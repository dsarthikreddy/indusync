' SARTHIK Room - opens the website. Starts it silently in the background first if it is not running.
' "/background" = only start it (used by Windows at sign-in), no browser window.
Option Explicit
Dim sh, fso, dir, py, i, background
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
dir = fso.GetParentFolderName(WScript.ScriptFullName)
sh.CurrentDirectory = dir
background = (WScript.Arguments.Count > 0)
py = dir & "\.venv\Scripts\python.exe"
If Not IsUp() Then
  If Not fso.FileExists(py) Then
    If Not background Then MsgBox "SARTHIK Room is not installed yet. Double-click Install.bat first.", 48, "SARTHIK Room"
    WScript.Quit
  End If
  If Not fso.FolderExists(dir & "\data") Then fso.CreateFolder(dir & "\data")
  sh.Environment("PROCESS")("CCTV_NO_BROWSER") = "1"
  sh.Run "cmd /c """"" & py & """ -m cctv.main > ""data\server.log"" 2>&1""", 0, False
  For i = 1 To 90
    WScript.Sleep 500
    If IsUp() Then Exit For
  Next
End If
If Not background Then sh.Run "http://localhost:" & Port() & "/"

Function Port()
  Dim f, t, re, m
  Port = "8000"
  If fso.FileExists(dir & "\data\settings.json") Then
    Set f = fso.OpenTextFile(dir & "\data\settings.json", 1)
    t = f.ReadAll : f.Close
    Set re = New RegExp : re.Pattern = """port""\s*:\s*(\d{2,5})"
    Set m = re.Execute(t)
    If m.Count > 0 Then Port = m(0).SubMatches(0)
  End If
End Function

Function IsUp()
  Dim h
  IsUp = False
  On Error Resume Next
  Set h = CreateObject("MSXML2.ServerXMLHTTP.6.0")
  h.setTimeouts 1500, 1500, 1500, 1500
  h.Open "GET", "http://127.0.0.1:" & Port() & "/login", False
  h.Send
  If Err.Number = 0 Then If h.Status > 0 And h.Status < 500 Then IsUp = True
  On Error GoTo 0
End Function
