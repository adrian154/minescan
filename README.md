# Minescan

Minescan is a tool for discovering Minecraft servers. It uses asynchronous IO to quickly scan large address ranges, collects information using the [server list ping](https://wiki.vg/Server_List_Ping) protocol, and stores the results in an SQLite database.

# Usage Notes

Minescan opens a lot of sockets at once, you probably want to raise the limit or you will encounter errors:

```
ulimit -Sn 100000
```

Reducing the number of SYN retries will also increase the number of hosts scanned per second.

```
sysctl -w net.ipv4.tcp_syn_retries=1
```

# Why?

Doing things through the kernel instead of using userspace networking (like [masscan](https://github.com/robertdavidgraham/masscan)) is objectively inferior&mdash;I made this project purely to get more familiar with epoll. It actually works surprisingly well though.