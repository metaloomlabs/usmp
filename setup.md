# Defender

```shell
netsh advfirewall set publicprofile state off
```

```shell
netsh advfirewall set publicprofile state on
```

```shell
netsh advfirewall firewall add rule name="USMP Server" dir=in action=allow protocol=TCP localport=9000 profile=any```

