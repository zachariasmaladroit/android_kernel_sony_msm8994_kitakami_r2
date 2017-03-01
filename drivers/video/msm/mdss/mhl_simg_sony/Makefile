CFLAGS_KERNEL := -fno-graphite -fno-graphite-identity -fno-loop-flatten -fno-tree-loop-linear -fno-loop-interchange -fno-loop-strip-mine -fno-loop-block -fno-loop-nest-optimize -fno-loop-parallelize-all -ftree-parallelize-loops=0 -fno-unsafe-math-optimizations -fno-strict-aliasing

# Base MHL drivers for platform glue
obj-y += mhl_platform_base.o
obj-y += mhl_platform_power.o
obj-y += mhl_platform.o
obj-y += mhl_platform_i2c.o

# MHL SII8620/SII8061 chip support
obj-y += mhl_sii8620_8061_device.o mhl_sii8620_device_edid.o
obj-y += mhl_driver.o mhl_tx.o mhl_tx_rcp.o
obj-y += mhl_lib_edid.o mhl_lib_infoframe.o mhl_lib_timer.o
obj-y += mhl_cbus_control.o mhl_cbus_write_burst.o


obj-$(CONFIG_MHL_SUPPORT_UNPOWERED_DONGLE) += mhl_support_unpowered_dongle.o
obj-$(CONFIG_MHL_BIST)			   += mhl_sii8620_bist.o
