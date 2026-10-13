set protocol_path="\"%~dp0ForzaProtocolSelector.exe\" \"%%1\""
reg add HKCU\Software\Classes\ForzaHorizon4ProtocolSelect /f /t REG_SZ /d "URL:ForzaHorizon4ProtocolSelect"
reg add HKCU\Software\Classes\ForzaHorizon4ProtocolSelect /f /v "URL Protocol" /t REG_SZ
reg add HKCU\Software\Classes\ForzaHorizon4ProtocolSelect\shell\open\command /f /t REG_SZ /d %protocol_path%
reg add HKLM\SOFTWARE\Microsoft\XboxLive\1985701699 /f /v "AcceptProtocol" /t REG_SZ /d "ForzaHorizon4ProtocolSelect://?1;{0};{1};{2}"
reg add HKLM\SOFTWARE\Microsoft\XboxLive\1985701699 /f /v "JoinProtocol" /t REG_SZ /d "ForzaHorizon4ProtocolSelect://?2;{0};{1};{2}"
pause