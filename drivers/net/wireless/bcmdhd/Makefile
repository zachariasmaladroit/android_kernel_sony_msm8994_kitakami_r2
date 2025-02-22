# bcmdhd
#####################
# Basic feature
#####################

DHDCFLAGS += -Wall -Wstrict-prototypes -Dlinux -DLINUX -DBCMDRIVER      \
        -DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DBCMFILEIMAGE      \
        -DDHDTHREAD -DDHD_BCMEVENTS -DSHOW_EVENTS -DBCMDBG -DWLP2P      \
        -DWIFI_ACT_FRAME -DARP_OFFLOAD_SUPPORT -DKEEP_ALIVE -DCSCAN     \
        -DPKT_FILTER_SUPPORT -DEMBEDDED_PLATFORM -DPNO_SUPPORT          \
        -DCONFIG_DTS

#####################
# Bus Interface Type
#####################
ifneq ($(CONFIG_BCMDHD_PCIE),)
	BUS_IFACE_PCIE=y
else
	BUS_IFACE_SDIO=y
endif

#####################
# SDIO I/F
#####################
ifeq ($(BUS_IFACE_SDIO),y)
	DHDCFLAGS += -DBDC -DOOB_INTR_ONLY -DDHD_BCMEVENTS -DMMC_SDIO_ABORT
	DHDCFLAGS += -DBCMSDIO -DBCMLXSDMMC -DUSE_SDIOFIFO_IOVAR
	DHDCFLAGS += -U__ARM_ARCH_7A__
	# DPC priority
	DHDCFLAGS += -DCUSTOM_DPC_PRIO_SETTING=98
	# idle count
	DHDCFLAGS += -DDHD_USE_IDLECOUNT
	# SKB TAILPAD to avoid out of boundary memory access
	DHDCFLAGS += -DDHDENABLE_TAILPAD
	DHDCFLAGS += -DSUPPORT_P2P_GO_PS
endif

#####################
# PCIE I/F
#####################
ifeq ($(BUS_IFACE_PCIE),y)
	DHDCFLAGS += -DPCIE_FULL_DONGLE -DBCMPCIE
	# DPC priority
	DHDCFLAGS += -DCUSTOM_DPC_PRIO_SETTING=-1
	# Enable Link down recovery
	DHDCFLAGS += -DSUPPORT_LINKDOWN_RECOVERY
	# Enable Firmware Coredump
	DHDCFLAGS += -DDHD_FW_COREDUMP
	# Enable packet audit at host side
	DHDCFLAGS += -DDHD_PKTID_AUDIT_ENABLED
endif



#################
# Common feature
#################

DHDCFLAGS += -DCUSTOMER_HW2 -DCUSTOMER_HW5
DHDCFLAGS += -DWL_CFG80211


# Debug
DHDCFLAGS += -DSIMPLE_MAC_PRINT
DHDCFLAGS += -DDEBUGFS_CFG80211
# Enable wakelock debug function
DHDCFLAGS += -DDHD_TRACE_WAKE_LOCK
# Print out kernel panic point of file and line info when assertion happened
DHDCFLAGS += -DBCMASSERT_LOG

# Print 8021X
DHDCFLAGS += -DDHD_8021X_DUMP

# VSDB
DHDCFLAGS += -DVSDB
DHDCFLAGS += -DPROP_TXSTATUS

# Wi-Fi Direct
DHDCFLAGS += -DWL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
# For p2p connection issue
DHDCFLAGS += -DWL_SCB_TIMEOUT=10
# For TDLS tear down inactive time 10 sec
DHDCFLAGS += -DCUSTOM_TDLS_IDLE_MODE_SETTING=10000
# for TDLS RSSI HIGH for establishing TDLS link
DHDCFLAGS += -DCUSTOM_TDLS_RSSI_THRESHOLD_HIGH=-80
# for TDLS RSSI HIGH for tearing down TDLS link
DHDCFLAGS += -DCUSTOM_TDLS_RSSI_THRESHOLD_LOW=-85

# Roaming
DHDCFLAGS += -DROAM_AP_ENV_DETECTION
DHDCFLAGS += -DROAM_ENABLE
DHDCFLAGS += -DENABLE_FW_ROAM_SUSPEND


# SoftAP
DHDCFLAGS += -DWL_SUPPORT_AUTO_CHANNEL -DSUPPORT_HIDDEN_AP
DHDCFLAGS += -DDISABLE_11H_SOFTAP
DHDCFLAGS += -DSUPPORT_HOSTAPD_BGN_MODE
DHDCFLAGS += -DDISABLE_WL_FRAMEBURST_SOFTAP
# For setting custom short & long retry limit
DHDCFLAGS += -DSET_RETRY_LIMIT -DCUSTOM_SRL_SETTING=13 -DCUSTOM_LRL_SETTING=13

# HW5 specific features
DHDCFLAGS += -DSUPPORT_PM2_ONLY
DHDCFLAGS += -DSUPPORT_AMPDU_MPDU_CMD
DHDCFLAGS += -DBLOCK_IPV6_PACKET -DPASS_IPV4_SUSPEND

# Support MAC ACL setting
DHDCFLAGS += -DWL_CFG80211_ACL
# Connection statistics
DHDCFLAGS += -DCONNECTION_STATISTICS
# END HW5 specific features


# For special PNO Event keep wake lock for 10sec
DHDCFLAGS += -DCUSTOM_PNO_EVENT_LOCK_xTIME=10
# For Passing all multicast packets to host when not in suspend mode.
DHDCFLAGS += -DPASS_ALL_MCAST_PKTS


# Early suspend
DHDCFLAGS += -DDHD_USE_EARLYSUSPEND

# WiFi turn off delay
DHDCFLAGS += -DWIFI_TURNOFF_DELAY=100

# For Scan result patch
DHDCFLAGS += -DESCAN_RESULT_PATCH
DHDCFLAGS += -DDUAL_ESCAN_RESULT_BUFFER
DHDCFLAGS += -DESCAN_BUF_OVERFLOW_MGMT

# For Static Buffer
ifeq ($(CONFIG_BROADCOM_WIFI_RESERVED_MEM),y)
  DHDCFLAGS += -DCONFIG_DHD_USE_STATIC_BUF
  DHDCFLAGS += -DENHANCED_STATIC_BUF
  DHDCFLAGS += -DSTATIC_WL_PRIV_STRUCT
ifeq ($(BUS_IFACE_PCIE),y)
  DHDCFLAGS += -DDHD_USE_STATIC_IOCTLBUF
endif
ifeq ($(CONFIG_BCMDHD_DEBUG_PAGEALLOC),y)
# Preallocation for memory dump
  DHDCFLAGS += -DDHD_USE_STATIC_MEMDUMP
endif
endif

# DTIM listen interval in suspend mode(0 means follow AP's DTIM period)
DHDCFLAGS += -DCUSTOM_SUSPEND_BCN_LI_DTIM=3

# Ioctl timeout 5000ms
DHDCFLAGS += -DIOCTL_RESP_TIMEOUT=5000

# Ioctl Rx count timeout
DHDCFLAGS += -DMAX_CNTL_RX_TIMEOUT=3

# Priority mismatch fix with kernel stack
DHDCFLAGS += -DPKTPRIO_OVERRIDE

# Prevent rx thread monopolize
DHDCFLAGS += -DWAIT_DEQUEUE

# Config PM Control
DHDCFLAGS += -DCONFIG_CONTROL_PM

# Use Android wake lock mechanism
DHDCFLAGS += -DCONFIG_HAS_WAKELOCK

# Used short dwell time during initial scan
DHDCFLAGS += -DUSE_INITIAL_SHORT_DWELL_TIME

DHDCFLAGS += -DWL_ABORT_SCAN

# Disable to delay link down event
DHDCFLAGS += -DDISABLE_BCN_DLY

##############################
# Android Platform Definition
##############################
BCM_SRC_DIR := ./

###########
# Lollipop
###########
DHDCFLAGS += -DWL_ENABLE_P2P_IF
#DHDCFLAGS += -DWL_SUPPORT_BACKPORTED_KPATCHES
# Default definitions for KitKat
DHDCFLAGS += -DWL_CFG80211_STA_EVENT
DHDCFLAGS += -DWL_IFACE_COMB_NUM_CHANNELS

ifeq ($(CONFIG_BCMDHD_INSMOD_NO_FW_LOAD),y)
  DHDCFLAGS += -DENABLE_INSMOD_NO_FW_LOAD
endif

ifneq ($(CONFIG_DHD_USE_SCHED_SCAN),)
DHDCFLAGS += -DWL_SCHED_SCAN
endif

# To support RTT
#DHDCFLAGS += -DRTT_SUPPORT
# To support Link Statictics
DHDCFLAGS += -DLINKSTAT_SUPPORT
# To support GSCAN
DHDCFLAGS += -DGSCAN_SUPPORT

# To support WL_VENDOR_EXT_SUPPORT
DHDCFLAGS += -DWL_VENDOR_EXT_SUPPORT

# Extra file list for Lollipop
ANDROID_OFILES := wl_cfgvendor.o


##########################
# driver type
# m: module type driver
# y: built-in type driver
##########################
DRIVER_TYPE ?= m

#########################
# Chip dependent feature
#########################

# Chipsets supported both SDIO and PCIE
ifneq ($(CONFIG_BCM4356),)
  DHDCFLAGS += -DSOMC_MIMO
  DHDCFLAGS += -DBCM4356_CHIP
  DHDCFLAGS += -DMIMO_ANT_SETTING
  DHDCFLAGS += -DENABLE_BCN_LI_BCN_WAKEUP
  DHDCFLAGS += -DDHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT

# tput enhancement for common
  DHDCFLAGS += -DUSE_WL_TXBF
  DHDCFLAGS += -DUSE_WL_FRAMEBURST
  DHDCFLAGS += -DPROP_TXSTATUS_VSDB
  DHDCFLAGS += -DDISABLE_FRAMEBURST_VSDB
  DHDCFLAGS += -DDISABLE_PM_BCNRX

# tput enhancement for SDIO
ifeq ($(BUS_IFACE_SDIO),y)
  DHDCFLAGS += -DHW_OOB
  DHDCFLAGS += -DCUSTOM_GLOM_SETTING=8 -DCUSTOM_RXCHAIN=1
  DHDCFLAGS += -DUSE_DYNAMIC_F2_BLKSIZE -DDYNAMIC_F2_BLKSIZE_FOR_NONLEGACY=128
  DHDCFLAGS += -DBCMSDIOH_TXGLOM -DCUSTOM_TXGLOM=1 -DBCMSDIOH_TXGLOM_HIGHSPEED
  DHDCFLAGS += -DRXFRAME_THREAD
  DHDCFLAGS += -DCUSTOM_AMPDU_BA_WSIZE=64 -DCUSTOM_IBSS_AMPDU_BA_WSIZE=16
  DHDCFLAGS += -DCUSTOM_DPC_CPUCORE=0
  DHDCFLAGS += -DCUSTOM_DEF_TXGLOM_SIZE=40 -DDHD_TXBOUND=40
  DHDCFLAGS += -DENABLE_ADAPTIVE_SCHED -DCUSTOM_CPUFREQ_THRESH=1000000
  DHDCFLAGS += -DCUSTOM_MAX_TXGLOM_SIZE=40
  DHDCFLAGS += -DMAX_HDR_READ=128
  DHDCFLAGS += -DDHD_FIRSTREAD=128
  DHDCFLAGS += -DCUSTOM_AMPDU_MPDU=16
  DHDCFLAGS += -DDHDTCPACK_SUPPRESS
endif

# tput enhancement for PCIE
ifeq ($(BUS_IFACE_PCIE),y)
  DHDCFLAGS += -DCUSTOM_AMPDU_BA_WSIZE=64 -DCUSTOM_IBSS_AMPDU_BA_WSIZE=16
  DHDCFLAGS += -DCUSTOM_AMPDU_MPDU=32
  DHDCFLAGS += -DCUSTOM_AMPDU_RELEASE=16
# DHDCFLAGS += -DCUSTOM_AMSDU_AGGSF=4
# DHDCFLAGS += -DSET_PCIEIRQ_CPU0
endif


ifeq ($(BUS_IFACE_PCIE),y)
  DHDCFLAGS += -DBCMPCIE_OOB_HOST_WAKE
  DHDCFLAGS += -DCUSTOM_DHD_WATCHDOG_MS=125
  DHDCFLAGS += -DDHD_USE_IDLECOUNT -DMAX_IDLE_COUNT=16 -DMAX_RESUME_WAIT=500
endif

ifeq ($(CONFIG_ARCH_MSM),y)
  #DHDCFLAGS += -DSET_RPS_CPUS
ifeq ($(BUS_IFACE_PCIE),y)
  DHDCFLAGS += -DBCMPCIE_DISABLE_ASYNC_SUSPEND
endif
endif

# New Features
  DHDCFLAGS += -DWL11U
  DHDCFLAGS += -DBCMCCX
  DHDCFLAGS += -DWLTDLS
  DHDCFLAGS += -DWLFBT
  DHDCFLAGS += -DDHD_ENABLE_LPC
  DHDCFLAGS += -DSUPPORT_LTECX
# DHDCFLAGS += -DSUPPORT_2G_VHT
  DHDCFLAGS += -DSUPPORT_WL_TXPOWER
# DHDCFLAGS += -DDISABLE_IF_COUNTERS
  DHDCFLAGS += -DDISABLE_TXBFR
ifeq ($(CONFIG_BCM4356),y)
  DHDCFLAGS += -DENABLE_INSMOD_NO_FW_LOAD
  DHDCFLAGS += -DUSE_LATE_INITCALL_SYNC
endif
ifeq ($(BUS_IFACE_SDIO),y)
  DHDCFLAGS += -DCUSTOM_PSPRETEND_THR=30
endif
endif

ifneq ($(CONFIG_BCM4354),)
  DHDCFLAGS += -DBCM4354_CHIP
  DHDCFLAGS += -DMIMO_ANT_SETTING
  DHDCFLAGS += -DUSE_CID_CHECK
  DHDCFLAGS += -DENABLE_BCN_LI_BCN_WAKEUP

# tput enhancement for common
  DHDCFLAGS += -DUSE_WL_TXBF
  DHDCFLAGS += -DUSE_WL_FRAMEBURST
  DHDCFLAGS += -DPROP_TXSTATUS_VSDB

# tput enhancement for SDIO
ifeq ($(BUS_IFACE_SDIO),y)
  DHDCFLAGS += -DHW_OOB
  DHDCFLAGS += -DCUSTOM_GLOM_SETTING=8 -DCUSTOM_RXCHAIN=1
  DHDCFLAGS += -DUSE_DYNAMIC_F2_BLKSIZE -DDYNAMIC_F2_BLKSIZE_FOR_NONLEGACY=128
  DHDCFLAGS += -DBCMSDIOH_TXGLOM -DCUSTOM_TXGLOM=1 -DBCMSDIOH_TXGLOM_HIGHSPEED
  DHDCFLAGS += -DRXFRAME_THREAD
  DHDCFLAGS += -DCUSTOM_AMPDU_BA_WSIZE=64 -DCUSTOM_IBSS_AMPDU_BA_WSIZE=16
  DHDCFLAGS += -DCUSTOM_DPC_CPUCORE=0
  DHDCFLAGS += -DCUSTOM_DEF_TXGLOM_SIZE=40 -DDHD_TXBOUND=40
  DHDCFLAGS += -DENABLE_ADAPTIVE_SCHED -DCUSTOM_CPUFREQ_THRESH=1000000
  DHDCFLAGS += -DCUSTOM_MAX_TXGLOM_SIZE=40
  DHDCFLAGS += -DMAX_HDR_READ=128
  DHDCFLAGS += -DDHD_FIRSTREAD=128
  DHDCFLAGS += -DCUSTOM_AMPDU_MPDU=16
  DHDCFLAGS += -DDHDTCPACK_SUPPRESS
endif

# tput enhancement for PCIE
ifeq ($(BUS_IFACE_PCIE),y)
  DHDCFLAGS += -DCUSTOM_AMPDU_BA_WSIZE=64 -DCUSTOM_IBSS_AMPDU_BA_WSIZE=16
  DHDCFLAGS += -DCUSTOM_AMPDU_MPDU=32
  DHDCFLAGS += -DCUSTOM_AMPDU_RELEASE=16
endif

ifeq ($(BUS_IFACE_PCIE),y)
  DHDCFLAGS += -DBCMPCIE_OOB_HOST_WAKE
endif


# New Features
  DHDCFLAGS += -DWL11U
  DHDCFLAGS += -DBCMCCX
  DHDCFLAGS += -DWLTDLS
  DHDCFLAGS += -DWLFBT
  DHDCFLAGS += -DDHD_ENABLE_LPC
# DHDCFLAGS += -DWLAIBSS
  DHDCFLAGS += -DSUPPORT_LTECX
#  DHDCFLAGS += -DSUPPORT_2G_VHT
  DHDCFLAGS += -DSUPPORT_WL_TXPOWER
ifeq ($(CONFIG_BCM4354),y)
  DHDCFLAGS += -DENABLE_INSMOD_NO_FW_LOAD
  DHDCFLAGS += -DUSE_LATE_INITCALL_SYNC
endif

ifeq ($(BUS_IFACE_SDIO),y)
  DHDCFLAGS += -DCUSTOM_PSPRETEND_THR=30
endif
endif

# Chipsets supported SDIO only
ifeq ($(BUS_IFACE_SDIO),y)
ifneq ($(CONFIG_BCM4339),)
  DHDCFLAGS += -DBCM4339_CHIP -DHW_OOB
  DHDCFLAGS += -DUSE_CID_CHECK
  DHDCFLAGS += -DENABLE_BCN_LI_BCN_WAKEUP
  DHDCFLAGS += -DUSE_SDIOFIFO_IOVAR

  # tput enhancement
  DHDCFLAGS += -DCUSTOM_GLOM_SETTING=8 -DCUSTOM_RXCHAIN=1
  DHDCFLAGS += -DUSE_DYNAMIC_F2_BLKSIZE -DDYNAMIC_F2_BLKSIZE_FOR_NONLEGACY=128
  DHDCFLAGS += -DBCMSDIOH_TXGLOM -DCUSTOM_TXGLOM=1 -DBCMSDIOH_TXGLOM_HIGHSPEED
  DHDCFLAGS += -DDHDTCPACK_SUPPRESS
  DHDCFLAGS += -DUSE_WL_TXBF
  DHDCFLAGS += -DUSE_WL_FRAMEBURST
  DHDCFLAGS += -DRXFRAME_THREAD
  DHDCFLAGS += -DCUSTOM_AMPDU_BA_WSIZE=64 -DCUSTOM_IBSS_AMPDU_BA_WSIZE=16
  DHDCFLAGS += -DCUSTOM_DPC_CPUCORE=0
  DHDCFLAGS += -DPROP_TXSTATUS_VSDB
ifeq ($(CONFIG_ARCH_MSM),y)
  DHDCFLAGS += -DCUSTOM_DEF_TXGLOM_SIZE=32 -DDHD_TXBOUND=32
  DHDCFLAGS += -DENABLE_ADAPTIVE_SCHED -DCUSTOM_CPUFREQ_THRESH=1000000
endif
  DHDCFLAGS += -DCUSTOM_MAX_TXGLOM_SIZE=32
  DHDCFLAGS += -DMAX_CNTL_TX_TIMEOUT=3

  # New Features
  DHDCFLAGS += -DWL11U
  DHDCFLAGS += -DBCMCCX
  DHDCFLAGS += -DWLTDLS
  DHDCFLAGS += -DWLFBT
  DHDCFLAGS += -DDHD_ENABLE_LPC
  DHDCFLAGS += -DSUPPORT_LTECX
# DHDCFLAGS += -DSUPPORT_2G_VHT
  DHDCFLAGS += -DSUPPORT_WL_TXPOWER
ifeq ($(CONFIG_BCM4339),y)
  DHDCFLAGS += -DENABLE_INSMOD_NO_FW_LOAD
  DHDCFLAGS += -DUSE_LATE_INITCALL_SYNC
endif
  DHDCFLAGS += -DCUSTOM_PSPRETEND_THR=30
  DHDCFLAGS += -DDISABLE_WL_FRAMEBURST_SOFTAP
endif
endif

#############################
# Platform dependent feature
#############################
# read custom mac address function
DHDCFLAGS += -DGET_CUSTOM_MAC_ENABLE

# Default Beacon timeout
ifneq ($(CONFIG_SOMC_WLAN_BCN_TIMEOUT),)
  DHDCFLAGS += -DSOMC_WLAN_BCN_TIMEOUT=$(CONFIG_SOMC_WLAN_BCN_TIMEOUT)
else
  DHDCFLAGS += -DSOMC_WLAN_BCN_TIMEOUT=3
endif

# The number of the maximum devices which phone can associate
DHDCFLAGS += -DSOMC_MAX_ASSOC_NUM=10

# Default Listen Interval in Beacons
ifneq ($(CONFIG_SOMC_WLAN_LISTEN_INTERVAL),)
  DHDCFLAGS += -DCUSTOM_LISTEN_INTERVAL=$(CONFIG_SOMC_WLAN_LISTEN_INTERVAL)
endif

# WAPI
DHDCFLAGS += -DBCMWAPI_WPI -DBCMWAPI_WAI

# Set keep alive period
ifneq ($(CONFIG_SOMC_WLAN_KEEP_ALIVE_SETTING),)
  DHDCFLAGS += -DCUSTOM_KEEP_ALIVE_SETTING=$(CONFIG_SOMC_WLAN_KEEP_ALIVE_SETTING)
endif

# Set the number of probe requests per channel
ifneq ($(CONFIG_SOMC_WLAN_SCAN_NPROBES),)
  DHDCFLAGS += -DSOMC_WLAN_SCAN_NPROBES=$(CONFIG_SOMC_WLAN_SCAN_NPROBES)
endif

# Set special NV paths for 1DK chip
ifneq ($(CONFIG_SOMC_WLAN_1DK_NV_PATH),)
  DHDCFLAGS += -DSOMC_1DK_NV_PATH=\"$(CONFIG_SOMC_WLAN_1DK_NV_PATH)\"
ifneq ($(CONFIG_SOMC_WLAN_LOW_POWER_NV_PATH),)
  DHDCFLAGS += -DSOMC_LOW_POWER_NV_PATH=\"$(CONFIG_SOMC_WLAN_LOW_POWER_NV_PATH)\"
endif
endif

# Disable to delay link down event
#ifeq ($(CONFIG_SOMC_WLAN_DISABLE_BCM_DLY),y)
#  DHDCFLAGS += -DDISABLE_BCN_DLY
#endif

# Change scan time
ifeq ($(CONFIG_SOMC_CFG_WLAN_CHANGE_SCAN_TIME),y)
  DHDCFLAGS += -DCHANGE_SCAN_TIME
endif

# Enable scan ps(scan power optimization)
ifeq ($(CONFIG_SOMC_WLAN_ENABLE_SCAN_PS),y)
  DHDCFLAGS += -DSOMC_WLAN_ENABLE_SCAN_PS
endif

# Set default nvram path
ifneq ($(CONFIG_SOMC_WLAN_NVRAM_PATH),)
  DHDCFLAGS += -DCONFIG_BCMDHD_NVRAM_PATH=\"$(CONFIG_SOMC_WLAN_NVRAM_PATH)\"
endif

# Enable Disconnection timing log
ifeq ($(CONFIG_SOMC_WLAN_ENABLE_DISC_TIME_LOG),y)
  DHDCFLAGS += -DDHD_ENABLE_DISC_TIME_LOG
endif

#########
# Others
#########


EXTRA_LDFLAGS += --strip-debug
EXTRA_CFLAGS += $(DHDCFLAGS) -DDHD_DEBUG
EXTRA_CFLAGS += -DSRCBASE=\"$(src)\"
EXTRA_CFLAGS += -I$(src)/include/ -I$(src)/
KBUILD_CFLAGS += -I$(LINUXDIR)/include -I$(shell pwd)

MODNAME := wlan

DHDOFILES := dhd_pno.o \
		dhd_common.o \
		dhd_ip.o \
		dhd_custom_gpio.o \
		dhd_linux.o \
		dhd_linux_sched.o \
		dhd_cfg80211.o \
		dhd_linux_wq.o \
		aiutils.o \
		bcmevent.o \
		bcmutils.o \
		bcmwifi_channels.o \
		hndpmu.o \
		linux_osl.o \
		sbutils.o \
		siutils.o \
		wl_android.o \
		wl_cfg80211.o \
		wl_cfgp2p.o \
		wl_cfg_btcoex.o \
		wldev_common.o \
		wl_linux_mon.o \
		wl_roam.o \
		dhd_linux_platdev.o \
		dhd_wlfc.o \
		hnd_pktq.o \
		hnd_pktpool.o \
		wl_cfgnan.o \
		bcmxtlv.o \
		dhd_somc_custom.o

ifeq ($(BUS_IFACE_SDIO),y)
DHDOFILES += bcmsdh.o \
		bcmsdh_linux.o \
		bcmsdh_sdmmc.o \
		bcmsdh_sdmmc_linux.o \
		dhd_cdc.o \
		dhd_sdio.o
endif


ifeq ($(BUS_IFACE_PCIE),y)
DHDOFILES += dhd_pcie.o \
		dhd_pcie_linux.o \
		pcie_core.o \
		dhd_flowring.o \
		dhd_msgbuf.o
endif


DHDOFILES += $(ANDROID_OFILES)

# Module information used by KBuild framework
obj-$(CONFIG_BCMDHD) += $(MODNAME).o

$(MODNAME)-objs := $(DHDOFILES)
