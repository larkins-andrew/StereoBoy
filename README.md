
Submodule cloning instructions:

```
git submodule init
git submodule update
```

sudo openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program blink.elf verify reset exit"