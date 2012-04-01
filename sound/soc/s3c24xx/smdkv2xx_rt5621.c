/*
 * smdks5p_rt5621.c
 *
 *  Copyright (c) 2009 Samsung Electronics Co. Ltd
 *  Author: Jaswinder Singh <jassi.brar@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-bank.h>
#include "../codecs/rt5621.h"
#include "s3c-dma.h"
//#include "s3c64xx-i2s.h"
#include <linux/io.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include "s3c64xx-i2s-v4.h"
#include <mach/regs-audss.h>
#include <linux/i2c.h>
//extern int Sound_Flag; //wang
#define CONFIG_SND_EEEBOT  //录音用的(开启和关闭影响不大)
//#undef CONFIG_SND_EEEBOT
extern struct snd_soc_platform s3c_dma_wrapper;
#if 0//
#define pr_debug(x...) printk("[Smdkv2xx_rt5621]: "x)
#define ur_debug(x...) printk(x)
//pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
#else
#define pr_debug(x...)	 do {} while(0)
#define ur_debug(x...)	do {} while(0)
#endif
#if 0 //wang
int Sound_Flag = 0; //wang
#include <linux/i2c.h>
extern i2c_register_board_info(int busnum,
	struct i2c_board_info const *info, unsigned len);
static struct i2c_board_info i2c_devs_wm8976[] __initdata = {

	 { I2C_BOARD_INFO("wm8976", 0x1a), }, //wang cut
};
#include <linux/i2c.h>
	//******wangyulu  add  up****/
	 static uint8_t config_lock_sleep[2]={0x00,0x07}; //
 #define GOODIX_KEY_I2C	(0x34>>1)
	 static int i2c_write_key(struct i2c_client *client,uint8_t *data,int len)
	 {
		struct i2c_msg msg;
		int ret=-1;
		//发送设备地址
		msg.flags=!I2C_M_RD;//写消息
		msg.addr=GOODIX_KEY_I2C;
		msg.len=len;
		msg.buf=data;		
		ret=i2c_transfer(i2c_get_adapter(1),&msg,1);
		if(ret <0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);	
		
		return ret;
	 }
	//************ urbetter+ ******************
	static int goodix_i2c_rxdata(char *rxdata, int length)
	{
		int ret;
		struct i2c_msg msgs[] = {
			{
				.addr	= GOODIX_KEY_I2C,
				.flags	= 0,
				.len = 1,
				.buf = rxdata,
			},
			{
				.addr	= GOODIX_KEY_I2C,
				.flags	= I2C_M_RD,
				.len = length,
				.buf = rxdata,
			},
		};
		ret = i2c_transfer(i2c_get_adapter(1), msgs, 2);//i2c_get_adapter(1)选着iic 1
		if (ret < 0)
			pr_err("msg %s i2c read error: %d\n", __func__, ret);
		
		return ret;
	}
#endif
//#define SMDK_RT5621_I2S_V5_PORT 	0 //yuanlai
#define SMDK_RT5621_I2S_V2_PORT 	1//wang 必须要选着濉 1 采用录音功能
#undef dev_dbg
#if defined(DEBUG)
#define dev_dbg(msg ...)	printk(KERN_DBG msg);
#else
#define dev_dbg(msg ...)
#endif
#define DINFO(format, arg...)\
do{\
  printk(" (%s)" format "\n" ,__FUNCTION__, ## arg);\
}while(0)

#define wait_stable(utime_out)					\
	do {							\
		if (!utime_out)					\
			utime_out = 1000;			\
		utime_out = loops_per_jiffy / HZ * utime_out;	\
		while (--utime_out) { 				\
			cpu_relax();				\
		}						\
	} while (0);

#if defined(DEBUG) 
static unsigned long get_epll_rate(void)
{
	pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	struct clk *fout_epll;
	unsigned long rate;

	fout_epll = clk_get(NULL, "fout_epll");
	if (IS_ERR(fout_epll)) {
		printk(KERN_ERR "%s: failed to get fout_epll\n", __func__);
		return -ENOENT;
	}

	rate = clk_get_rate(fout_epll);

	clk_put(fout_epll);

	return rate;
}
#endif 

static void swv2_enable_output()
{
     pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	u32 cfg;
	cfg = readl(S5PV210_GPH0DAT);
	cfg |= 0x1 <<2;  //set AU_EN to 1, chagre a Vout..
	writel(cfg, S5PV210_GPH0DAT);
	udelay(1000);

}
static void swv2_disable_output()
{
     pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	u32 cfg;
	cfg = readl(S5PV210_GPH0DAT);
	cfg &= ~(0x8 <<2);  //set ALOM to 0, shutdown AMP output
	writel(cfg, S5PV210_GPH0DAT);
	udelay(1000);

}
static int set_epll_rate(unsigned long rate)
{
	pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);

	struct clk *fout_epll;
	//unsigned int wait_utime = 500;
	unsigned int wait_utime = 10; //urbetter

	fout_epll = clk_get(NULL, "fout_epll");
	if (IS_ERR(fout_epll)) {
		printk(KERN_ERR "%s: failed to get fout_epll\n", __func__);
		return -ENOENT;
	}

	if (rate == clk_get_rate(fout_epll)) {
		printk("set_epll_rate, rate=%d\n", clk_get_rate(fout_epll));
		goto out;
	}

	clk_disable(fout_epll);
	wait_stable(wait_utime);

	clk_set_rate(fout_epll, rate);
	wait_stable(wait_utime);

	clk_enable(fout_epll);

out:
	clk_put(fout_epll);

	return 0;
}

//RT5621 is in Slave Mode
static int smdk_socmst_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct s3c_i2sv2_rate_calc div;
	unsigned int epll_out_rate;
	int ret;
	int rfs, bfs, psr, rclk;
    	pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	//wang
#if 0//wang add
     unsigned char buff[2];
	buff[0] = 0x7E;
	ret = goodix_i2c_rxdata(buff, 1);
	printk("wang&&&&&&&&&&rt5621_read&&&&&&&&&&&&&&&&&&key=0x%02X\n", buff[0]); 
	printk("wang&&&&&&&&&&rt5621_read&&&&&&&&&&&&&&&&&&key=0x%02X\n", ret); 
#endif
	printk("urbetter rate=%d, epll_rate=%d\n", params_rate(params), epll_out_rate);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		return -EINVAL;
	}
	switch (params_rate(params)) {
	case 16000:
	case 22050:
	case 24000:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		if (bfs == 48)
			rfs = 384;
		else
			rfs = 256;//
		break;
	case 64000:
		rfs = 384;
		break;
	case 8000:
	case 11025:
	case 12000:
	//case 44100: //wang add
		if (bfs == 48)
			rfs = 768;
		else
			rfs = 512;
		break;
	default:
		return -EINVAL;
	}
     
	rclk = params_rate(params) * rfs;//44100 * 256 wang cut
	switch (rclk) {
	case 4096000:
	case 5644800:
	case 6144000:
	case 8467200:
	case 9216000:
		psr = 8;
		break;
	case 8192000:
	case 11289600:
	case 12288000:
	case 16934400:
	case 18432000:
		psr = 4;
		break;
	case 22579200:
	case 24576000:
	case 33868800:
	case 36864000:
		psr = 2;
		break;
	case 67737600:
	case 73728000:
		psr = 1;
		break;
	default:
		printk("Not yet supported!\n");
		return -EINVAL;
	}
	epll_out_rate = rclk * psr;//44100 * 512 * 2
	//printk("wang yu lu   epll_out=%d, rclk=%d, psr=%d\n", epll_out_rate, rclk, psr);


	/* Set EPLL clock rate */
	ret = set_epll_rate(epll_out_rate);
	if (ret < 0) {
		printk(KERN_ERR "%s: set epll rate failed\n", __func__);
		return ret;
	}
	/* Set the Codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		printk(KERN_ERR "%s: set codec dai failed\n", __func__);
		return ret;
	}
	/* Set the AP DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
					 | SND_SOC_DAIFMT_NB_NF
					 | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		printk(KERN_ERR "%s: set cpu dai failed\n", __func__);
		return ret;
	}
	ret = snd_soc_dai_set_sysclk(cpu_dai, S3C64XX_CLKSRC_CDCLK,
					0, SND_SOC_CLOCK_OUT);//wang
	/* We use SCLK_AUDIO for basic ops in SoC-Master mode */
	ret = snd_soc_dai_set_sysclk(cpu_dai, S3C64XX_CLKSRC_MUX,
					0, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "%s: set CLKSRC for cpu dai failed\n", __func__);
		return ret;
	}
	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C_I2SV2_DIV_RCLK, rfs);
	if (ret < 0) {
		printk(KERN_ERR "%s:set clk divider RCLK for cpu dai failed\n", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C_I2SV2_DIV_BCLK, bfs);
	if (ret < 0) {
		printk(KERN_ERR "%s:set clk divider BCLK for cpu dai failed\n", __func__);
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C_I2SV2_DIV_PRESCALER,psr-1);
	if (ret < 0) {
		printk(KERN_ERR "%s:set clk prescaler for cpu dai failed\n", __func__);
		return ret;
	}
    //mdelay(1000);
    mdelay(20);
	return 0;
}

/*
 * SMDK RT5621 DAI operations.
 */
static struct snd_soc_ops smdk_i2s_ops = {
	.hw_params = smdk_socmst_hw_params,
};

/* SMDK Playback widgets */
static const struct snd_soc_dapm_widget rt5621_dapm_widgets_pbk[] = {
	SND_SOC_DAPM_HP("Front-L/R", NULL),
	SND_SOC_DAPM_HP("Center/Sub", NULL),
	SND_SOC_DAPM_HP("Rear-L/R", NULL),
};

/* SMDK Capture widgets */
static const struct snd_soc_dapm_widget rt5621_dapm_widgets_cpt[] = {
	SND_SOC_DAPM_MIC("MicIn", NULL),
	SND_SOC_DAPM_LINE("LineIn", NULL),
};

//JY.Lai -- we only support I2Sv2 connection
static struct snd_soc_dai_link smdkrt5621_dai[] = {
	{
		.name = "RT5621 I2S SAIF",
		//.name = "RT5621 HiFi",
		.stream_name = "Tx/Rx",
		.cpu_dai = &s3c64xx_i2s_v4_dai[SMDK_RT5621_I2S_V2_PORT],
		.codec_dai = &rt5621_dai,
		.ops = &smdk_i2s_ops,
	},
};
static struct snd_soc_card smdk = {
	.name = "smdk",
	.platform = &s3c_dma_wrapper,
	.dai_link = smdkrt5621_dai,
	.num_links = ARRAY_SIZE(smdkrt5621_dai),
};

static struct rt5621_setup_data smdk_rt5621_setup = {
	.i2c_address = (0x34>>1),//wang
     .i2c_bus = 1, //wang
};

static struct snd_soc_device smdk_snd_devdata = {
	.card = &smdk,
	.codec_dev = &soc_codec_dev_rt5621,
	.codec_data = &smdk_rt5621_setup,
};

static struct platform_device *smdk_snd_device;

#ifdef CONFIG_SND_EEEBOT //具有录音功能
int eeebot_board_enable_amp()
{
	u32 cfg;
	 pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	cfg = readl(S5PV210_GPH1DAT);
	cfg &= ~( (0x1 << 1) | (0x1 <<6));
	writel(cfg, S5PV210_GPH1DAT);
	udelay(10);
	cfg |= (0x1 << 1);
	writel(cfg, S5PV210_GPH1DAT);
	udelay(10);
	cfg |= (0x1 << 6);
	writel(cfg, S5PV210_GPH1DAT);

	return 0;
}
EXPORT_SYMBOL_GPl(eeebot_board_enable_amp);

int eeebot_board_enable_sound()
{
 	pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);

	u32 cfg;
	cfg = readl(S5PV210_GPH0DAT);

	cfg |= (0x01 << 1); //GPH0(1)
	writel(cfg, S5PV210_GPH0DAT);
	return 0;
}
EXPORT_SYMBOL_GPL(eeebot_board_enable_sound);

int eeebot_board_sound_init()
{
 	pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);

	u32 cfg;
	cfg = readl(S5PV210_GPH1CON);
	cfg &= ~(S5P_GPIO_CONMASK(1) | S5P_GPIO_CONMASK(6));
	cfg |= (S5P_GPIO_OUTPUT(1) | S5P_GPIO_OUTPUT(6));
	writel(cfg, S5PV210_GPH1CON);

	cfg = readl(S5PV210_GPH0CON);
	cfg &= ~(S5P_GPIO_CONMASK(1));
	cfg |= S5P_GPIO_OUTPUT(1);
	writel(cfg, S5PV210_GPH0CON);
	eeebot_board_enable_amp();
	return 0;
}


EXPORT_SYMBOL_GPL(eeebot_board_sound_init);
#endif
static void check_i2s_board_type(void)
{
      pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	extern char g_selected_pcb[];
	if(!strcmp(g_selected_pcb, "single") || !strcmp(g_selected_pcb, "cv05"))
	{
		printk("smdk2xx_rt5621::->check_i2s_board_type  detected single board type PCB / cv05.\n");
		smdkrt5621_dai[0].cpu_dai = &s3c64xx_i2s_v4_dai[0];
	}
}

static int __init smdk_audio_init(void)
{
 	pr_debug("smdkv2xx_rt5621::::::::: %s\n", __func__);
	int ret;
     extern char g_selected_codec[];
     #if 0 //wang
	int *clk_5621  = 0xc06674fc;//指针的地址
     clk_enable(clk_5621); //wang explain 是打开5621或8976的Mclk c06674fc
    
	unsigned char buff[2];
	buff[0] = 0x7E;
	ret = goodix_i2c_rxdata(buff, 1);
	printk("wang&&&&&&&&&&rt5621_read&&&&&&&&&&&&&&&&&&key=0x%02X\n", buff[0]); 
	printk("wang&&&&&&&&&&rt5621_read&&&&&&&&&&&&&&&&&&key=0x%02X\n", ret); 
	if(buff[0] == 0x21)
		{
	        printk("wang&&&&&&&&&&rt5621_add\n"); 
		   Sound_Flag = 0;
		}
	else
		{
		  printk("wang&&&&&&&&&&wm8976_add\n"); 
		  i2c_register_board_info(1, i2c_devs_wm8976, ARRAY_SIZE(i2c_devs_wm8976));
		  Sound_Flag = 1;
		  return 0;
		}
	#endif
	#if 1 //wang cut
	//printk("smdkv2xx_rt5621:::->:smdk_audio_init&&&&&Sound_Flag=0x%02X\n", Sound_Flag); 
	if(!strcmp(g_selected_codec, "rt5621"))
	{
	        printk("smdkv2xx_rt5621:::->:smdk_audio_init........5621 ok\n");
	  
     }
	else{
		   printk("smdkv2xx_rt5621:::->:smdk_audio_init........5621 out\n");
	        return 0;
		}
	#endif
	smdk_snd_device = platform_device_alloc("soc-audio", -1);//wang change
	if (!smdk_snd_device)
		{
		   return -ENOMEM;
		   pr_debug("smdkv2xx_rt5621 2323232::::::::: %s\n", __func__);
		}
	 check_i2s_board_type( ); //wang add
	 
	platform_set_drvdata(smdk_snd_device, &smdk_snd_devdata);
	smdk_snd_devdata.dev = &smdk_snd_device->dev;

	ret = platform_device_add(smdk_snd_device);
	if (ret)
		platform_device_put(smdk_snd_device);
#ifdef CONFIG_SND_EEEBOT
	eeebot_board_sound_init();
#endif

	return ret;
}

static void __exit smdk_audio_exit(void)
{
	platform_device_unregister(smdk_snd_device);
}
module_init(smdk_audio_init);
module_exit(smdk_audio_exit);

MODULE_AUTHOR("Jaswinder Singh, jassi.brar@samsung.com");
MODULE_DESCRIPTION("ALSA SoC SMDK RT5621");
MODULE_LICENSE("GPL");
