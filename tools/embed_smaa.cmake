# Embed the reference shader without shell escaping (cmd.exe and POSIX sh disagree on backslashes).
file(READ "${INPUT}" text)
string(REPLACE "\r" "" text "${text}")
string(REGEX REPLACE "[^\t\n -~]" "?" text "${text}")
string(REPLACE "\\" "\\\\" text "${text}")
string(REPLACE "\"" "\\\"" text "${text}")
string(REPLACE "\n" "\\n\"\n\"" text "${text}")
file(WRITE "${OUTPUT}" "\"${text}\"\n")
