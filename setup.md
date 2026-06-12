# Defender

```shell
netsh advfirewall set publicprofile state off
```

```shell
netsh advfirewall set publicprofile state on
```

```shell
New-NetFirewallRule -DisplayName "USMP Server" -Direction Inbound -Protocol TCP -LocalPort 9000 -Action Allow
```