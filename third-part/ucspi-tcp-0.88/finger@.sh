echo "${2-}" | HOME/bin/tcpclient -RHl0 -- "${1-0}" 79 sh -c '
  HOME/bin/addcr >&7
  exec HOME/bin/delcr <&6
' | cat -v
