/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "mot_maui_ov02b10mipiraw_Sensor.h"

#define PFX "mot_maui_ov02b10"
#define LOG_INF(format, args...)    \
    pr_debug(PFX "[%s] " format, __func__, ##args)
#define LOG_ERR(format, args...)    \
    pr_err(PFX "[%s] " format, __func__, ##args)

static calibration_status_t mnf_status = CRC_FAILURE;
static calibration_status_t af_status = CRC_FAILURE;
static calibration_status_t awb_status = CRC_FAILURE;
static calibration_status_t lsc_status = CRC_FAILURE;
static calibration_status_t pdaf_status = CRC_FAILURE;
static calibration_status_t dual_status = CRC_FAILURE;
/* Camera Hardwareinfo */
//extern struct global_otp_struct hw_info_main2_otp;
static kal_uint32 streaming_control(kal_bool enable);
static DEFINE_SPINLOCK(imgsensor_drv_lock);

static struct imgsensor_info_struct imgsensor_info = {
    .sensor_id = MOT_MAUI_OV02B10_SENSOR_ID,
    .checksum_value = 0xb7c53a42,       //0x6d01485c // Auto Test Mode 蓄板..

    .pre = {
    	.pclk = 16500000,            //record different mode's pclk
    	.linelength = 448,            //record different mode's linelength
    	.framelength = 1240,            //record different mode's framelength
    	.startx = 0,                    //record different mode's startx of grabwindow
    	.starty = 0,                    //record different mode's starty of grabwindow
    	.grabwindow_width = 1600,        //record different mode's width of grabwindow
    	.grabwindow_height = 1200,        //record different mode's height of grabwindow
    	/*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario    */
    	.mipi_data_lp2hs_settle_dc = 85,//unit , ns
    	/*     following for GetDefaultFramerateByScenario()    */
    	.mipi_pixel_rate = 66000000,
    	.max_framerate = 297,
    },
    .cap = {
    	.pclk = 16500000,            //record different mode's pclk
    	.linelength = 448,            //record different mode's linelength
    	.framelength = 1240,            //record different mode's framelength
    	.startx = 0,                    //record different mode's startx of grabwindow
    	.starty = 0,                    //record different mode's starty of grabwindow
    	.grabwindow_width = 1600,        //record different mode's width of grabwindow
    	.grabwindow_height = 1200,        //record different mode's height of grabwindow
    	/*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario    */
    	.mipi_data_lp2hs_settle_dc = 85,//unit , ns
    	/*     following for GetDefaultFramerateByScenario()    */
    	.mipi_pixel_rate = 66000000,
    	.max_framerate = 297,
    },
    .normal_video = {
    	.pclk = 16500000,            //record different mode's pclk
    	.linelength = 448,            //record different mode's linelength
    	.framelength = 1240,            //record different mode's framelength
    	.startx = 0,                    //record different mode's startx of grabwindow
    	.starty = 0,                    //record different mode's starty of grabwindow
    	.grabwindow_width = 1600,        //record different mode's width of grabwindow
    	.grabwindow_height = 1200,        //record different mode's height of grabwindow
    	.mipi_data_lp2hs_settle_dc = 85,//unit , ns
    	.mipi_pixel_rate = 66000000,
    	.max_framerate = 297,
    },

    .margin = 7,            //sensor framelength & shutter margin
    .min_shutter = 4,        //min shutter
    .max_frame_length = 0x7fff,//max framelength by sensor register's limitation
    .ae_shut_delay_frame = 0,    //shutter delay frame for AE cycle, 2 frame with ispGain_delay-shut_delay=2-0=2
    .ae_sensor_gain_delay_frame = 0,//sensor gain delay frame for AE cycle,2 frame with ispGain_delay-sensor_gain_delay=2-0=2
    .ae_ispGain_delay_frame = 2,//isp gain delay frame for AE cycle

    .ihdr_support = 0,      //1, support; 0,not support
    .ihdr_le_firstline = 0,  //1,le first ; 0, se first
    .sensor_mode_num = 3,

    .cap_delay_frame = 2,
    .pre_delay_frame = 2,
    .video_delay_frame = 2,
    .frame_time_delay_frame = 1,
    .isp_driving_current = ISP_DRIVING_6MA, //mclk driving current
    .sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,//sensor_interface_type
    .mipi_sensor_type = MIPI_OPHY_NCSI2, //0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2
    .mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,//0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL
    .sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_B,//sensor output first pixel color
    .mclk = 24,//mclk value, suggest 24 or 26 for 24Mhz or 26Mhz
    .mipi_lane_num = SENSOR_MIPI_1_LANE,//mipi lane num
    .i2c_addr_table = {0x78,0xff},
    .i2c_speed = 400,
};


static struct imgsensor_struct imgsensor = {
    .mirror = IMAGE_NORMAL,                //mirrorflip information
    .sensor_mode = IMGSENSOR_MODE_INIT, //IMGSENSOR_MODE enum value,record current sensor mode,such as: INIT, Preview, Capture, Video,High Speed Video, Slim Video
    .shutter = 0x3D0,                    //current shutter
    .gain = 0x100,                        //current gain
    .dummy_pixel = 0,                    //current dummypixel
    .dummy_line = 0,                    //current dummyline
    .current_fps = 300,  //full size current fps : 24fps for PIP, 30fps for Normal or ZSD
    .autoflicker_en = KAL_FALSE,  //auto flicker enable: KAL_FALSE for disable auto flicker, KAL_TRUE for enable auto flicker
    .test_pattern = KAL_FALSE,        //test pattern mode or not. KAL_FALSE for in test pattern mode, KAL_TRUE for normal output
    .current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,//current scenario id
    .ihdr_en = 0, //sensor need support LE, SE with HDR feature
    .i2c_write_id = 0x78,//record current sensor's i2c write id
};


/* Sensor output window information */
static struct SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[3]={
    {  1600, 1200,  0, 0, 1600, 1200, 1600, 1200, 0000, 0000, 1600, 1200,  0, 0, 1600, 1200}, // Preview
    {  1600, 1200,  0, 0, 1600, 1200, 1600, 1200, 0000, 0000, 1600, 1200,  0, 0, 1600, 1200}, // capture
    {  1600, 1200,  0, 0, 1600, 1200, 1600, 1200, 0000, 0000, 1600, 1200,  0, 0, 1600, 1200}  // video
};

static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
    kal_uint16 get_byte=0;
    char pu_send_cmd[1] = {(char)(addr & 0xFF)};
    iReadRegI2C(pu_send_cmd, 1, (u8*)&get_byte, 1, imgsensor.i2c_write_id);

    return get_byte;
}

static void write_cmos_sensor(kal_uint32 addr, kal_uint32 para)
{
    char pu_send_cmd[2] = {(char)(addr & 0xFF), (char)(para & 0xFF)};
    iWriteRegI2C(pu_send_cmd, 2, imgsensor.i2c_write_id);
}


static void set_dummy(void)
{
    kal_uint32 v_blank = 0;

    if (imgsensor.frame_length%2 != 0) {
    	imgsensor.frame_length = imgsensor.frame_length - imgsensor.frame_length % 2;
    }

    v_blank = ((imgsensor.frame_length-0x4c4) < 0x14) ? 0x14 : (imgsensor.frame_length-0x4c4);

    LOG_INF("imgsensor.frame_length = %d, v_blank = %d\n", imgsensor.frame_length, v_blank);

    write_cmos_sensor(0xfd, 0x01);
    write_cmos_sensor(0x14, (v_blank & 0x7F00) >> 8);
    write_cmos_sensor(0x15, v_blank & 0xFF);
    write_cmos_sensor(0xfe, 0x02);//fresh
}    /*    set_dummy  */

static kal_uint32 return_sensor_id(void)
{
    write_cmos_sensor(0xfd, 0x00);
    return ((read_cmos_sensor(0x02) << 8) | read_cmos_sensor(0x03));
}
static void set_max_framerate(UINT16 framerate,kal_bool min_framelength_en)
{
    kal_uint32 frame_length = imgsensor.frame_length;


    frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;

    spin_lock(&imgsensor_drv_lock);
    imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
    imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
    {
        imgsensor.frame_length = imgsensor_info.max_frame_length;
        imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    }
    if (min_framelength_en)
        imgsensor.min_frame_length = imgsensor.frame_length;

    spin_unlock(&imgsensor_drv_lock);

    set_dummy();
}    /*    set_max_framerate  */

static void write_shutter(kal_uint32 shutter)
{
    kal_uint32 realtime_fps = 0;
    kal_uint32 v_blank;

    spin_lock(&imgsensor_drv_lock);

    if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
        imgsensor.frame_length = shutter + imgsensor_info.margin;
    else
    	imgsensor.frame_length = imgsensor.min_frame_length;
    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
        imgsensor.frame_length = imgsensor_info.max_frame_length;

    spin_unlock(&imgsensor_drv_lock);

    shutter = (shutter < imgsensor_info.min_shutter) ?
        imgsensor_info.min_shutter : shutter;
    shutter =
        (shutter > (imgsensor_info.max_frame_length -
        imgsensor_info.margin)) ? (imgsensor_info.max_frame_length -
        imgsensor_info.margin) : shutter;

//frame_length and shutter should be an even number.
    shutter = (shutter >> 1) << 1;
    imgsensor.frame_length = (imgsensor.frame_length >> 1) << 1;
//auroflicker:need to avoid 15fps and 30 fps
    if (imgsensor.autoflicker_en == KAL_TRUE) {
        realtime_fps = imgsensor.pclk /
            imgsensor.line_length * 10 / imgsensor.frame_length;
        if (realtime_fps >= 297 && realtime_fps <= 305) {
            realtime_fps = 296;
            set_max_framerate(realtime_fps, 0);
        } else if (realtime_fps >= 147 && realtime_fps <= 150) {
            realtime_fps = 146;
            set_max_framerate(realtime_fps, 0);
        } else {
            imgsensor.frame_length = (imgsensor.frame_length  >> 1) << 1;
    		v_blank = ((imgsensor.frame_length-0x4c4) < 0x14) ? 0x14 : (imgsensor.frame_length-0x4c4);
                        write_cmos_sensor(0xfd, 0x01);
    		write_cmos_sensor(0x14, (v_blank >> 8) & 0x7F);
    		write_cmos_sensor(0x15, v_blank & 0xFF);
    		write_cmos_sensor(0xfe, 0x02);	//fresh
    	}
    } else {
    	imgsensor.frame_length = (imgsensor.frame_length  >> 1) << 1;
    	v_blank = ((imgsensor.frame_length-0x4c4) < 0x14) ? 0x14 : (imgsensor.frame_length-0x4c4);
    	write_cmos_sensor(0xfd, 0x01);
    	write_cmos_sensor(0x14, (v_blank >> 8) & 0x7F);
    	write_cmos_sensor(0x15, v_blank & 0xFF);
        write_cmos_sensor(0xfe, 0x02);//fresh
   }

    write_cmos_sensor(0xfd, 0x01);
    write_cmos_sensor(0x0e, (shutter >> 8) & 0xFF);
    write_cmos_sensor(0x0f, shutter  & 0xFF);
    write_cmos_sensor(0xfe, 0x02);//fresh sss

    LOG_INF("shutter =%d, framelength =%d, v_blank = %d\n", shutter, imgsensor.frame_length, v_blank);
}

static void set_shutter(kal_uint32 shutter)
{
    unsigned long flags;
    spin_lock_irqsave(&imgsensor_drv_lock, flags);
    imgsensor.shutter = shutter;
    spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

    write_shutter(shutter);
    }
static void set_shutter_frame_length(kal_uint16 shutter,
    		kal_uint16 frame_length)
{
    kal_uint16 realtime_fps = 0;
    kal_int32 dummy_line = 0;
    unsigned long flags;
    kal_uint32 v_blank = 0;

    spin_lock_irqsave(&imgsensor_drv_lock, flags);
    imgsensor.shutter = shutter;
    spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

    spin_lock(&imgsensor_drv_lock);
    if (frame_length > 1)
    	dummy_line = frame_length - imgsensor.frame_length;
    imgsensor.frame_length = imgsensor.frame_length + dummy_line;

    if (shutter > imgsensor.frame_length - imgsensor_info.margin)
    	imgsensor.frame_length = shutter + imgsensor_info.margin;

    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
    	imgsensor.frame_length = imgsensor_info.max_frame_length;

    spin_unlock(&imgsensor_drv_lock);

    shutter = (shutter < imgsensor_info.min_shutter) ?
    	imgsensor_info.min_shutter : shutter;
    shutter =
    	(shutter > (imgsensor_info.max_frame_length -
    	imgsensor_info.margin)) ? (imgsensor_info.max_frame_length -
    	imgsensor_info.margin) : shutter;

    //frame_length and shutter should be an even number.
    shutter = (shutter >> 1) << 1;
    imgsensor.frame_length = (imgsensor.frame_length >> 1) << 1;
//auroflicker:need to avoid 15fps and 30 fps
    if (imgsensor.autoflicker_en == KAL_TRUE) {
    	realtime_fps = imgsensor.pclk /
    		imgsensor.line_length * 10 / imgsensor.frame_length;
    	if (realtime_fps >= 297 && realtime_fps <= 305) {
    		realtime_fps = 296;
        set_max_framerate(realtime_fps, 0);
    	} else if (realtime_fps >= 147 && realtime_fps <= 150) {
    		realtime_fps = 146;
        set_max_framerate(realtime_fps, 0);
    	} else {
    		imgsensor.frame_length = (imgsensor.frame_length  >> 1) << 1;
    		v_blank = ((imgsensor.frame_length-0x4c4) < 0x14) ? 0x14 : (imgsensor.frame_length-0x4c4);
    		write_cmos_sensor(0xfd, 0x01);
    		write_cmos_sensor(0x14, (v_blank >> 8) & 0x7F);
    		write_cmos_sensor(0x15, v_blank & 0xFF);
    		write_cmos_sensor(0xfe, 0x02);
    	}
    } else {
    	imgsensor.frame_length = (imgsensor.frame_length  >> 1) << 1;
    	v_blank = ((imgsensor.frame_length-0x4c4) < 0x14) ? 0x14 : (imgsensor.frame_length-0x4c4);
    	write_cmos_sensor(0xfd, 0x01);
    	write_cmos_sensor(0x14, (v_blank >> 8) & 0x7F);
    	write_cmos_sensor(0x15, v_blank & 0xFF);
    	write_cmos_sensor(0xfe, 0x02);
    }

    /* Update Shutter */
     write_cmos_sensor(0xfd, 0x01);
     write_cmos_sensor(0x0e, (shutter >> 8) & 0xFF);
     write_cmos_sensor(0x0f, shutter  & 0xFF);
     write_cmos_sensor(0xfe, 0x02);

    LOG_INF("shutter =%d, framelength =%d, realtime_fps =%d, v_blank = %d\n",
    	shutter, imgsensor.frame_length, realtime_fps, v_blank);
}    			/* set_shutter_frame_length */




/*************************************************************************
 * FUNCTION
 *    set_gain
 *
 * DESCRIPTION
 *    This function is to set global gain to sensor.
 *
 * PARAMETERS
 *    iGain : sensor global gain(base: 0x40)
 *
 * RETURNS
 *    the actually gain set to sensor.
 *
 * GLOBALS AFFECTED
 *
 *************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain)
{
    kal_uint8  iReg;

    if((gain >= 0x40) && (gain <= (15*0x40))) //base gain = 0x40
    {
        iReg = 0x10 * gain/BASEGAIN;        //change mtk gain base to aptina gain base

        if(iReg<=0x10)
        {
            write_cmos_sensor(0xfd, 0x01);
            write_cmos_sensor(0x22, 0x10);//0x23
            write_cmos_sensor(0xfe, 0x02);//fresh
            LOG_INF("OV02BMIPI_SetGain = 16");
        }
        else if(iReg>= 0xf8)//gpw
        {
            write_cmos_sensor(0xfd, 0x01);
            write_cmos_sensor(0x22,0xf8);
            write_cmos_sensor(0xfe, 0x02);//fresh
            LOG_INF("OV02BMIPI_SetGain = 160");
        }
        else
        {
            write_cmos_sensor(0xfd, 0x01);
            write_cmos_sensor(0x22, (kal_uint8)iReg);
            write_cmos_sensor(0xfe, 0x02);//fresh
            LOG_INF("OV02BMIPI_SetGain = %d",iReg);
        }
    }
    else
        LOG_INF("error gain setting");

    return gain;
}    /*    set_gain  */

static void ihdr_write_shutter_gain(kal_uint16 le, kal_uint16 se, kal_uint16 gain)
{
    LOG_INF("le: 0x%x, se: 0x%x, gain: 0x%x\n", le, se, gain);
}

static void night_mode(kal_bool enable)
{
     /* No Need to implement this function */
}


static void sensor_init(void)
{
    write_cmos_sensor(0xfc, 0x01);
    write_cmos_sensor(0xfd, 0x00);
    write_cmos_sensor(0xfd, 0x00);
    write_cmos_sensor(0x24, 0x02);
    write_cmos_sensor(0x25, 0x06);
    write_cmos_sensor(0x29, 0x03);
    write_cmos_sensor(0x2a, 0x34);
    write_cmos_sensor(0x1e, 0x17);
    write_cmos_sensor(0x33, 0x07);
    write_cmos_sensor(0x35, 0x07);
    write_cmos_sensor(0x4a, 0x0c);
    write_cmos_sensor(0x3a, 0x05);
    write_cmos_sensor(0x3b, 0x02);
    write_cmos_sensor(0x3e, 0x00);
    write_cmos_sensor(0x46, 0x01);
    write_cmos_sensor(0x6d, 0x03);
    write_cmos_sensor(0xfd, 0x01);
    write_cmos_sensor(0x0e, 0x02);
    write_cmos_sensor(0x0f, 0x1a);
    write_cmos_sensor(0x15, 0x14);
    write_cmos_sensor(0x18, 0x00);
    write_cmos_sensor(0x22, 0xff);
    write_cmos_sensor(0x23, 0x02);
    write_cmos_sensor(0x17, 0x2c);
    write_cmos_sensor(0x19, 0x20);
    write_cmos_sensor(0x1b, 0x06);
    write_cmos_sensor(0x1c, 0x04);
    write_cmos_sensor(0x20, 0x03);
    write_cmos_sensor(0x30, 0x01);
    write_cmos_sensor(0x33, 0x01);
    write_cmos_sensor(0x31, 0x0a);
    write_cmos_sensor(0x32, 0x09);
    write_cmos_sensor(0x38, 0x01);
    write_cmos_sensor(0x39, 0x01);
    write_cmos_sensor(0x3a, 0x01);
    write_cmos_sensor(0x3b, 0x01);
    write_cmos_sensor(0x4f, 0x04);
    write_cmos_sensor(0x4e, 0x05);
    write_cmos_sensor(0x50, 0x01);
    write_cmos_sensor(0x35, 0x0c);
    write_cmos_sensor(0x45, 0x2a);
    write_cmos_sensor(0x46, 0x2a);
    write_cmos_sensor(0x47, 0x2a);
    write_cmos_sensor(0x48, 0x2a);
    write_cmos_sensor(0x4a, 0x2c);
    write_cmos_sensor(0x4b, 0x2c);
    write_cmos_sensor(0x4c, 0x2c);
    write_cmos_sensor(0x4d, 0x2c);
    write_cmos_sensor(0x56, 0x3a);
    write_cmos_sensor(0x57, 0x0a);
    write_cmos_sensor(0x58, 0x24);
    write_cmos_sensor(0x59, 0x20);
    write_cmos_sensor(0x5a, 0x0a);
    write_cmos_sensor(0x5b, 0xff);
    write_cmos_sensor(0x37, 0x0a);
    write_cmos_sensor(0x42, 0x0e);
    write_cmos_sensor(0x68, 0x90);
    write_cmos_sensor(0x69, 0xcd);
    write_cmos_sensor(0x6a, 0x8f);
    write_cmos_sensor(0x7c, 0x0a);
    write_cmos_sensor(0x7d, 0x0a);
    write_cmos_sensor(0x7e, 0x0a);
    write_cmos_sensor(0x7f, 0x08);
    write_cmos_sensor(0x83, 0x14);
    write_cmos_sensor(0x84, 0x14);
    write_cmos_sensor(0x86, 0x14);
    write_cmos_sensor(0x87, 0x07);
    write_cmos_sensor(0x88, 0x0f);
    write_cmos_sensor(0x94, 0x02);
    write_cmos_sensor(0x98, 0xd1);
    write_cmos_sensor(0xfe, 0x02);
    write_cmos_sensor(0xfd, 0x03);
    write_cmos_sensor(0x97,0x6c);
    write_cmos_sensor(0x98,0x60);
    write_cmos_sensor(0x99,0x60);
    write_cmos_sensor(0x9a,0x6c);
    write_cmos_sensor(0xa1,0x40);
    write_cmos_sensor(0xaf,0x04);
    write_cmos_sensor(0xb1,0x40);
    write_cmos_sensor(0xae, 0x0d);
    write_cmos_sensor(0x88,0x5b);
    write_cmos_sensor(0x89, 0x7c);
    write_cmos_sensor(0xb4, 0x05);
    write_cmos_sensor(0x8c, 0x40);
    write_cmos_sensor(0x8e, 0x40);
    write_cmos_sensor(0x90, 0x40);
    write_cmos_sensor(0x92, 0x40);
    write_cmos_sensor(0x9b,0x46);
    write_cmos_sensor(0xac, 0x40);
    write_cmos_sensor(0xfd, 0x00);
    write_cmos_sensor(0x5a, 0x15);
    write_cmos_sensor(0x74, 0x01);
    write_cmos_sensor(0xfd, 0x00);
    write_cmos_sensor(0x50, 0x40);
    write_cmos_sensor(0x52, 0xb0);
    write_cmos_sensor(0xfd, 0x01);
    write_cmos_sensor(0x03, 0x70);
    write_cmos_sensor(0x05, 0x10);
    write_cmos_sensor(0x07, 0x20);
    write_cmos_sensor(0x09, 0xb0);
    write_cmos_sensor(0xfd, 0x03);
    write_cmos_sensor(0xc2, 0x01);
    write_cmos_sensor(0xfb, 0x01);
}
/*    MIPI_sensor_Init  */

static void preview_setting(void)
{
write_cmos_sensor(0xfc, 0x01);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x24, 0x02);
write_cmos_sensor(0x25, 0x06);
write_cmos_sensor(0x29, 0x03);
write_cmos_sensor(0x2a, 0x34);
write_cmos_sensor(0x1e, 0x17);
write_cmos_sensor(0x33, 0x07);
write_cmos_sensor(0x35, 0x07);
write_cmos_sensor(0x4a, 0x0c);
write_cmos_sensor(0x3a, 0x05);
write_cmos_sensor(0x3b, 0x02);
write_cmos_sensor(0x3e, 0x00);
write_cmos_sensor(0x46, 0x01);
write_cmos_sensor(0x6d, 0x03);
write_cmos_sensor(0xfd, 0x01);
write_cmos_sensor(0x0e, 0x02);
write_cmos_sensor(0x0f, 0x1a);
write_cmos_sensor(0x15, 0x14);
write_cmos_sensor(0x18, 0x00);
write_cmos_sensor(0x22, 0xff);
write_cmos_sensor(0x23, 0x02);
write_cmos_sensor(0x17, 0x2c);
write_cmos_sensor(0x19, 0x20);
write_cmos_sensor(0x1b, 0x06);
write_cmos_sensor(0x1c, 0x04);
write_cmos_sensor(0x20, 0x03);
write_cmos_sensor(0x30, 0x01);
write_cmos_sensor(0x33, 0x01);
write_cmos_sensor(0x31, 0x0a);
write_cmos_sensor(0x32, 0x09);
write_cmos_sensor(0x38, 0x01);
write_cmos_sensor(0x39, 0x01);
write_cmos_sensor(0x3a, 0x01);
write_cmos_sensor(0x3b, 0x01);
write_cmos_sensor(0x4f, 0x04);
write_cmos_sensor(0x4e, 0x05);
write_cmos_sensor(0x50, 0x01);
write_cmos_sensor(0x35, 0x0c);
write_cmos_sensor(0x45, 0x2a);
write_cmos_sensor(0x46, 0x2a);
write_cmos_sensor(0x47, 0x2a);
write_cmos_sensor(0x48, 0x2a);
write_cmos_sensor(0x4a, 0x2c);
write_cmos_sensor(0x4b, 0x2c);
write_cmos_sensor(0x4c, 0x2c);
write_cmos_sensor(0x4d, 0x2c);
write_cmos_sensor(0x56, 0x3a);
write_cmos_sensor(0x57, 0x0a);
write_cmos_sensor(0x58, 0x24);
write_cmos_sensor(0x59, 0x20);
write_cmos_sensor(0x5a, 0x0a);
write_cmos_sensor(0x5b, 0xff);
write_cmos_sensor(0x37, 0x0a);
write_cmos_sensor(0x42, 0x0e);
write_cmos_sensor(0x68, 0x90);
write_cmos_sensor(0x69, 0xcd);
write_cmos_sensor(0x6a, 0x8f);
write_cmos_sensor(0x7c,0x0a);
write_cmos_sensor(0x7d,0x0a);
write_cmos_sensor(0x7e,0x0a);
write_cmos_sensor(0x7f, 0x08);
write_cmos_sensor(0x83, 0x14);
write_cmos_sensor(0x84, 0x14);
write_cmos_sensor(0x86, 0x14);
write_cmos_sensor(0x87, 0x07);
write_cmos_sensor(0x88, 0x0f);
write_cmos_sensor(0x94, 0x02);
write_cmos_sensor(0x98, 0xd1);
write_cmos_sensor(0xfe, 0x02);
write_cmos_sensor(0xfd, 0x03);
write_cmos_sensor(0x97,0x6c);
write_cmos_sensor(0x98,0x60);
write_cmos_sensor(0x99,0x60);
write_cmos_sensor(0x9a,0x6c);
write_cmos_sensor(0xa1,0x40);
write_cmos_sensor(0xaf,0x04);
write_cmos_sensor(0xb1,0x40);
write_cmos_sensor(0xae, 0x0d);
write_cmos_sensor(0x88,0x5b);
write_cmos_sensor(0x89, 0x7c);
write_cmos_sensor(0xb4, 0x05);
write_cmos_sensor(0x8c, 0x40);
write_cmos_sensor(0x8e, 0x40);
write_cmos_sensor(0x90, 0x40);
write_cmos_sensor(0x92, 0x40);
write_cmos_sensor(0x9b,0x46);
write_cmos_sensor(0xac, 0x40);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x5a, 0x15);
write_cmos_sensor(0x74, 0x01);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x50, 0x40);
write_cmos_sensor(0x52, 0xb0);
write_cmos_sensor(0xfd, 0x01);
write_cmos_sensor(0x03, 0x70);
write_cmos_sensor(0x05, 0x10);
write_cmos_sensor(0x07, 0x20);
write_cmos_sensor(0x09, 0xb0);
write_cmos_sensor(0xfd, 0x03);
write_cmos_sensor(0xc2, 0x01);
write_cmos_sensor(0xfb, 0x01);
}


static void capture_setting(kal_uint16 currefps)
{
write_cmos_sensor(0xfc, 0x01);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x24, 0x02);
write_cmos_sensor(0x25, 0x06);
write_cmos_sensor(0x29, 0x03);
write_cmos_sensor(0x2a, 0x34);
write_cmos_sensor(0x1e, 0x17);
write_cmos_sensor(0x33, 0x07);
write_cmos_sensor(0x35, 0x07);
write_cmos_sensor(0x4a, 0x0c);
write_cmos_sensor(0x3a, 0x05);
write_cmos_sensor(0x3b, 0x02);
write_cmos_sensor(0x3e, 0x00);
write_cmos_sensor(0x46, 0x01);
write_cmos_sensor(0x6d, 0x03);
write_cmos_sensor(0xfd, 0x01);
write_cmos_sensor(0x0e, 0x02);
write_cmos_sensor(0x0f, 0x1a);
write_cmos_sensor(0x15, 0x14);
write_cmos_sensor(0x18, 0x00);
write_cmos_sensor(0x22, 0xff);
write_cmos_sensor(0x23, 0x02);
write_cmos_sensor(0x17, 0x2c);
write_cmos_sensor(0x19, 0x20);
write_cmos_sensor(0x1b, 0x06);
write_cmos_sensor(0x1c, 0x04);
write_cmos_sensor(0x20, 0x03);
write_cmos_sensor(0x30, 0x01);
write_cmos_sensor(0x33, 0x01);
write_cmos_sensor(0x31, 0x0a);
write_cmos_sensor(0x32, 0x09);
write_cmos_sensor(0x38, 0x01);
write_cmos_sensor(0x39, 0x01);
write_cmos_sensor(0x3a, 0x01);
write_cmos_sensor(0x3b, 0x01);
write_cmos_sensor(0x4f, 0x04);
write_cmos_sensor(0x4e, 0x05);
write_cmos_sensor(0x50, 0x01);
write_cmos_sensor(0x35, 0x0c);
write_cmos_sensor(0x45, 0x2a);
write_cmos_sensor(0x46, 0x2a);
write_cmos_sensor(0x47, 0x2a);
write_cmos_sensor(0x48, 0x2a);
write_cmos_sensor(0x4a, 0x2c);
write_cmos_sensor(0x4b, 0x2c);
write_cmos_sensor(0x4c, 0x2c);
write_cmos_sensor(0x4d, 0x2c);
write_cmos_sensor(0x56, 0x3a);
write_cmos_sensor(0x57, 0x0a);
write_cmos_sensor(0x58, 0x24);
write_cmos_sensor(0x59, 0x20);
write_cmos_sensor(0x5a, 0x0a);
write_cmos_sensor(0x5b, 0xff);
write_cmos_sensor(0x37, 0x0a);
write_cmos_sensor(0x42, 0x0e);
write_cmos_sensor(0x68, 0x90);
write_cmos_sensor(0x69, 0xcd);
write_cmos_sensor(0x6a, 0x8f);
write_cmos_sensor(0x7c,0x0a);
write_cmos_sensor(0x7d,0x0a);
write_cmos_sensor(0x7e,0x0a);
write_cmos_sensor(0x7f, 0x08);
write_cmos_sensor(0x83, 0x14);
write_cmos_sensor(0x84, 0x14);
write_cmos_sensor(0x86, 0x14);
write_cmos_sensor(0x87, 0x07);
write_cmos_sensor(0x88, 0x0f);
write_cmos_sensor(0x94, 0x02);
write_cmos_sensor(0x98, 0xd1);
write_cmos_sensor(0xfe, 0x02);
write_cmos_sensor(0xfd, 0x03);
write_cmos_sensor(0x97,0x6c);
write_cmos_sensor(0x98,0x60);
write_cmos_sensor(0x99,0x60);
write_cmos_sensor(0x9a,0x6c);
write_cmos_sensor(0xa1,0x40);
write_cmos_sensor(0xaf,0x04);
write_cmos_sensor(0xb1,0x40);
write_cmos_sensor(0xae, 0x0d);
write_cmos_sensor(0x88,0x5b);
write_cmos_sensor(0x89, 0x7c);
write_cmos_sensor(0xb4, 0x05);
write_cmos_sensor(0x8c, 0x40);
write_cmos_sensor(0x8e, 0x40);
write_cmos_sensor(0x90, 0x40);
write_cmos_sensor(0x92, 0x40);
write_cmos_sensor(0x9b,0x46);
write_cmos_sensor(0xac, 0x40);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x5a, 0x15);
write_cmos_sensor(0x74, 0x01);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x50, 0x40);
write_cmos_sensor(0x52, 0xb0);
write_cmos_sensor(0xfd, 0x01);
write_cmos_sensor(0x03, 0x70);
write_cmos_sensor(0x05, 0x10);
write_cmos_sensor(0x07, 0x20);
write_cmos_sensor(0x09, 0xb0);
write_cmos_sensor(0xfd, 0x03);
write_cmos_sensor(0xc2, 0x01);
write_cmos_sensor(0xfb, 0x01);
}    /*    capture_setting  */

static void normal_video_setting(void)
{
write_cmos_sensor(0xfc, 0x01);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x24, 0x02);
write_cmos_sensor(0x25, 0x06);
write_cmos_sensor(0x29, 0x03);
write_cmos_sensor(0x2a, 0x34);
write_cmos_sensor(0x1e, 0x17);
write_cmos_sensor(0x33, 0x07);
write_cmos_sensor(0x35, 0x07);
write_cmos_sensor(0x4a, 0x0c);
write_cmos_sensor(0x3a, 0x05);
write_cmos_sensor(0x3b, 0x02);
write_cmos_sensor(0x3e, 0x00);
write_cmos_sensor(0x46, 0x01);
write_cmos_sensor(0x6d, 0x03);
write_cmos_sensor(0xfd, 0x01);
write_cmos_sensor(0x0e, 0x02);
write_cmos_sensor(0x0f, 0x1a);
write_cmos_sensor(0x15, 0x14);
write_cmos_sensor(0x18, 0x00);
write_cmos_sensor(0x22, 0xff);
write_cmos_sensor(0x23, 0x02);
write_cmos_sensor(0x17, 0x2c);
write_cmos_sensor(0x19, 0x20);
write_cmos_sensor(0x1b, 0x06);
write_cmos_sensor(0x1c, 0x04);
write_cmos_sensor(0x20, 0x03);
write_cmos_sensor(0x30, 0x01);
write_cmos_sensor(0x33, 0x01);
write_cmos_sensor(0x31, 0x0a);
write_cmos_sensor(0x32, 0x09);
write_cmos_sensor(0x38, 0x01);
write_cmos_sensor(0x39, 0x01);
write_cmos_sensor(0x3a, 0x01);
write_cmos_sensor(0x3b, 0x01);
write_cmos_sensor(0x4f, 0x04);
write_cmos_sensor(0x4e, 0x05);
write_cmos_sensor(0x50, 0x01);
write_cmos_sensor(0x35, 0x0c);
write_cmos_sensor(0x45, 0x2a);
write_cmos_sensor(0x46, 0x2a);
write_cmos_sensor(0x47, 0x2a);
write_cmos_sensor(0x48, 0x2a);
write_cmos_sensor(0x4a, 0x2c);
write_cmos_sensor(0x4b, 0x2c);
write_cmos_sensor(0x4c, 0x2c);
write_cmos_sensor(0x4d, 0x2c);
write_cmos_sensor(0x56, 0x3a);
write_cmos_sensor(0x57, 0x0a);
write_cmos_sensor(0x58, 0x24);
write_cmos_sensor(0x59, 0x20);
write_cmos_sensor(0x5a, 0x0a);
write_cmos_sensor(0x5b, 0xff);
write_cmos_sensor(0x37, 0x0a);
write_cmos_sensor(0x42, 0x0e);
write_cmos_sensor(0x68, 0x90);
write_cmos_sensor(0x69, 0xcd);
write_cmos_sensor(0x6a, 0x8f);
write_cmos_sensor(0x7c,0x0a);
write_cmos_sensor(0x7d,0x0a);
write_cmos_sensor(0x7e,0x0a);
write_cmos_sensor(0x7f, 0x08);
write_cmos_sensor(0x83, 0x14);
write_cmos_sensor(0x84, 0x14);
write_cmos_sensor(0x86, 0x14);
write_cmos_sensor(0x87, 0x07);
write_cmos_sensor(0x88, 0x0f);
write_cmos_sensor(0x94, 0x02);
write_cmos_sensor(0x98, 0xd1);
write_cmos_sensor(0xfe, 0x02);
write_cmos_sensor(0xfd, 0x03);
write_cmos_sensor(0x97,0x6c);
write_cmos_sensor(0x98,0x60);
write_cmos_sensor(0x99,0x60);
write_cmos_sensor(0x9a,0x6c);
write_cmos_sensor(0xa1,0x40);
write_cmos_sensor(0xaf,0x04);
write_cmos_sensor(0xb1,0x40);
write_cmos_sensor(0xae, 0x0d);
write_cmos_sensor(0x88,0x5b);
write_cmos_sensor(0x89, 0x7c);
write_cmos_sensor(0xb4, 0x05);
write_cmos_sensor(0x8c, 0x40);
write_cmos_sensor(0x8e, 0x40);
write_cmos_sensor(0x90, 0x40);
write_cmos_sensor(0x92, 0x40);
write_cmos_sensor(0x9b,0x46);
write_cmos_sensor(0xac, 0x40);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x5a, 0x15);
write_cmos_sensor(0x74, 0x01);
write_cmos_sensor(0xfd, 0x00);
write_cmos_sensor(0x50, 0x40);
write_cmos_sensor(0x52, 0xb0);
write_cmos_sensor(0xfd, 0x01);
write_cmos_sensor(0x03, 0x70);
write_cmos_sensor(0x05, 0x10);
write_cmos_sensor(0x07, 0x20);
write_cmos_sensor(0x09, 0xb0);
write_cmos_sensor(0xfd, 0x03);
write_cmos_sensor(0xc2, 0x01);

write_cmos_sensor(0xfb, 0x01);
}

#define OV02B10_OTP_SIZE 31
unsigned char ov02b10_otp_data[OV02B10_OTP_SIZE] = {0};
#define OV02B10_OTP_DATA_PATH "/data/vendor/camera_dump/mot_maui_ov02b10_otp.bin"
#define MAUI_OV02B10_OTP_CRC_AWB_GROUP1_CAL_SIZE 7
#define MAUI_OV02B10_OTP_CRC_AWB_GROUP2_CAL_SIZE 6
#define OV02B10_AWB_DATA_SIZE 15
#define OV02B10_SERIAL_NUM_SIZE 16
#define DEPTH_SERIAL_NUM_DATA_PATH "/data/vendor/camera_dump/serial_number_macro.bin"
static void ov02b10_otp_dump_bin(const char *file_name, uint32_t size, const void *data)
{
    struct file *fp = NULL;
    mm_segment_t old_fs;
    int ret = 0;

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    fp = filp_open(file_name, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
    if (IS_ERR_OR_NULL(fp)) {
    	ret = PTR_ERR(fp);
    	LOG_INF("open file error(%s), error(%d)\n",  file_name, ret);
    	goto p_err;
    }

    ret = vfs_write(fp, (const char *)data, size, &fp->f_pos);
    if (ret < 0) {
    	LOG_INF("file write fail(%s) to EEPROM data(%d)", file_name, ret);
    	goto p_err;
    }

    LOG_INF("wirte to file(%s)\n", file_name);
p_err:
    if (!IS_ERR_OR_NULL(fp))
    	filp_close(fp, NULL);

    set_fs(old_fs);
    LOG_INF(" end writing file");
}

static int32_t eeprom_util_check_crc16(uint8_t *data, uint32_t size, uint32_t ref_crc)
{
	int32_t crc_match = 0;
	uint8_t crc = 0x00;
	uint32_t i;
	uint32_t tmp = 0;
	/* Calculate both methods of CRC since integrators differ on
	* how CRC should be calculated. */
	for (i = 0; i < size; i++) {
	    tmp += data[i];
	}
	crc = tmp%0xff + 1;
	if (crc == ref_crc)
		crc_match = 1;
	LOG_INF("REF_CRC 0x%x CALC CRC 0x%x  matches? %d\n",
		ref_crc, crc, crc_match);
	return crc_match;
}

static calibration_status_t MAUI_OV02B10_check_awb_data(void *data)
{
    unsigned char *data_awb = data; //add flag and checksum value
    if(((data_awb[0]&0xC0)>>6) == 0x01){ //Bit[7:6] 01:Valid 11:Invalid
    	LOG_INF("awb data is group1\n");
    	if(!eeprom_util_check_crc16(&data_awb[0],
		MAUI_OV02B10_OTP_CRC_AWB_GROUP1_CAL_SIZE,
		data_awb[7])) {
		LOG_INF("AWB CRC Fails!");
		return CRC_FAILURE;
		}
    } else if(((data_awb[0]&0x30)>>4) == 0x01){ //Bit[5:4] 01:Valid 11:Invalid
    	LOG_INF("awb data is group2\n");
    	if(!eeprom_util_check_crc16(&data_awb[8],
		MAUI_OV02B10_OTP_CRC_AWB_GROUP2_CAL_SIZE,
		data_awb[14])) {
		LOG_INF("AWB CRC Fails!");
		return CRC_FAILURE;
		}
    } else {
    	LOG_INF("ov02b10 OTP has no awb data\n");
    	return CRC_FAILURE;
    }
    LOG_INF("AWB CRC Pass");
    return NO_ERRORS;
}


static void MAUI_OV02B10_eeprom_format_calibration_data(void *data)
{
	if (NULL == data) {
	    LOG_INF("data is NULL");
	    return;
	}
	mnf_status            = 0;
	af_status             = 0;
	awb_status            = MAUI_OV02B10_check_awb_data(data);
	lsc_status            = 0;
	pdaf_status           = 0;
	dual_status           = 0;
	LOG_INF("status mnf:%d, af:%d, awb:%d, lsc:%d, pdaf:%d, dual:%d",
		mnf_status, af_status, awb_status, lsc_status, pdaf_status, dual_status);
}

static int ov02b10_read_data_from_otp(void)
{
    int i=0;
    LOG_INF("ov02b10_read_data_from_otp -E");
    write_cmos_sensor(0xfd, 0x06);
    write_cmos_sensor(0x21, 0x00);
    write_cmos_sensor(0x2f, 0x01);
    for(i=0;i<OV02B10_OTP_SIZE;i++)
    {
    	ov02b10_otp_data[i]=read_cmos_sensor(i);
    }

    LOG_INF("ov02b10_read_data_from_otp -X");
    return 0;
}


static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
    kal_uint8 i = 0;
    kal_uint8 retry = 2;

    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
        spin_lock(&imgsensor_drv_lock);
        imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
        spin_unlock(&imgsensor_drv_lock);
        do {
            *sensor_id = return_sensor_id();
            if (*sensor_id == imgsensor_info.sensor_id) {
                /*
    			if(compareManufacturerId() != 0) {
                    *sensor_id = 0xFFFFFFFF;
                    LOG_ERR("compare Manufacturer Id error\n");
                    return ERROR_SENSOR_CONNECT_FAIL;
                }
    			*/
                LOG_ERR("ov02b10 i2c write id : 0x%x, sensor id: 0x%x\n",
                imgsensor.i2c_write_id, *sensor_id);
                ov02b10_read_data_from_otp();
                ov02b10_otp_dump_bin(OV02B10_OTP_DATA_PATH, OV02B10_AWB_DATA_SIZE, (void *)&ov02b10_otp_data[16]);
                ov02b10_otp_dump_bin(DEPTH_SERIAL_NUM_DATA_PATH, OV02B10_SERIAL_NUM_SIZE, (void *)ov02b10_otp_data);
                MAUI_OV02B10_eeprom_format_calibration_data((void *)&ov02b10_otp_data[16]);
                return ERROR_NONE;
            }

            retry--;
    	} while (retry > 0);
    	i++;
        retry = 2;
    }
    if (*sensor_id != imgsensor_info.sensor_id) {
        LOG_ERR("ov02b10 Read id fail,sensor id: 0x%x\n", *sensor_id);
        *sensor_id = 0xFFFFFFFF;
        return ERROR_SENSOR_CONNECT_FAIL;
    }

    return ERROR_NONE;
}

static kal_uint32 open(void)
{
    kal_uint8 i = 0;
    kal_uint8 retry = 2;
    kal_uint16 sensor_id = 0;
    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
        spin_lock(&imgsensor_drv_lock);
        imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
        spin_unlock(&imgsensor_drv_lock);
        do {
            sensor_id = return_sensor_id();
            if (sensor_id == imgsensor_info.sensor_id) {
                LOG_INF("ov02b10 i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id, sensor_id);
                break;
            }

            retry--;
        } while(retry > 0);
        i++;
        if (sensor_id == imgsensor_info.sensor_id)
            break;
        retry = 2;
    }
    if (imgsensor_info.sensor_id != sensor_id)
        return ERROR_SENSOR_CONNECT_FAIL;
    /* initail sequence write in  */
    sensor_init();

    spin_lock(&imgsensor_drv_lock);

    imgsensor.autoflicker_en= KAL_FALSE;
    imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.dummy_pixel = 0;
    imgsensor.dummy_line = 0;
    imgsensor.ihdr_en = 0;
    imgsensor.test_pattern = KAL_FALSE;
    imgsensor.current_fps = imgsensor_info.pre.max_framerate;
    spin_unlock(&imgsensor_drv_lock);

    return ERROR_NONE;
}    /*    open  */

static kal_uint32 close(void)
{
    LOG_INF("E\n");
    streaming_control(KAL_FALSE);
    /*No Need to implement this function*/
    return ERROR_NONE;
}    /*	close  */

static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
    	MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
        pr_info("[ov02b] preview mode start\n");
    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    preview_setting();
    return ERROR_NONE;
}    /*    preview   */

static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
    	MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
        pr_info("[ov02b] capture mode start\n");
    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;

    imgsensor.pclk = imgsensor_info.cap.pclk;
    imgsensor.line_length = imgsensor_info.cap.linelength;
    imgsensor.frame_length = imgsensor_info.cap.framelength;
    imgsensor.min_frame_length = imgsensor_info.cap.framelength;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    LOG_INF("Caputre fps:%d\n", imgsensor.current_fps);
    capture_setting(imgsensor.current_fps);

    return ERROR_NONE;
}    /* capture() */
static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
    	MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
    imgsensor.pclk = imgsensor_info.normal_video.pclk;
    imgsensor.line_length = imgsensor_info.normal_video.linelength;
    imgsensor.frame_length = imgsensor_info.normal_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
    /* imgsensor.current_fps = 300; */
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    normal_video_setting();
    return ERROR_NONE;
}    /*    normal_video   */

static kal_uint32 get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *sensor_resolution)
{
    sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
    sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;
    sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
    sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;
    sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
    sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;
    return ERROR_NONE;
}    /*    get_resolution    */

static kal_uint32 get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
    	MSDK_SENSOR_INFO_STRUCT *sensor_info,
    	MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("scenario_id = %d\n", scenario_id);

    sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
    sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW; /* inverse with datasheet */
    sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorInterruptDelayLines = 4; /* not use */
    sensor_info->SensorResetActiveHigh = FALSE; /* not use */
    sensor_info->SensorResetDelayCount = 5; /* not use */

    sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
    sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
    sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
    sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

    sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
    sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
    sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;

    sensor_info->SensorMasterClockSwitch = 0; /* not use */
    sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;
    sensor_info->FrameTimeDelayFrame =
    	imgsensor_info.frame_time_delay_frame;
    sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;
    sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;
    sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
    sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
    sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
    sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

    sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
    sensor_info->SensorClockFreq = imgsensor_info.mclk;
    sensor_info->SensorClockDividCount = 3; /* not use */
    sensor_info->SensorClockRisingCount = 0;
    sensor_info->SensorClockFallingCount = 2; /* not use */
    sensor_info->SensorPixelClockCount = 3; /* not use */
    sensor_info->SensorDataLatchCount = 2; /* not use */

    sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->SensorWidthSampling = 0;  // 0 is default 1x
    sensor_info->SensorHightSampling = 0;    // 0 is default 1x
    sensor_info->SensorPacketECCOrder = 1;
    sensor_info->calibration_status.mnf = mnf_status;
    sensor_info->calibration_status.af = af_status;
    sensor_info->calibration_status.awb = awb_status;
    sensor_info->calibration_status.lsc = lsc_status;
    sensor_info->calibration_status.pdaf = pdaf_status;
    sensor_info->calibration_status.dual = dual_status;
    {
    	snprintf(sensor_info->mnf_calibration.serial_number, MAX_CALIBRATION_STRING,
    		"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
    	ov02b10_otp_data[0], ov02b10_otp_data[1],
    	ov02b10_otp_data[2], ov02b10_otp_data[3],
    	ov02b10_otp_data[4], ov02b10_otp_data[5],
    	ov02b10_otp_data[6], ov02b10_otp_data[7],
    	ov02b10_otp_data[8], ov02b10_otp_data[9],
    	ov02b10_otp_data[10], ov02b10_otp_data[11],
    	ov02b10_otp_data[12], ov02b10_otp_data[13],
    	ov02b10_otp_data[14], ov02b10_otp_data[15]);
    }
    switch (scenario_id) {
    case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
        sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
        sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

        sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
    			imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
    break;
    case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
        sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
        sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;

        sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
    		imgsensor_info.cap.mipi_data_lp2hs_settle_dc;
    break;
    case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
        sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
        sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;

        sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
    		imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;
    	break;
    default:
        sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
        sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

        sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
            imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
    break;
    }

    return ERROR_NONE;
}    /*    get_info  */

static kal_uint32 control(enum MSDK_SCENARIO_ID_ENUM scenario_id,
            MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
            MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("scenario_id = %d\n", scenario_id);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.current_scenario_id = scenario_id;
    spin_unlock(&imgsensor_drv_lock);
    switch (scenario_id) {
    case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
        preview(image_window, sensor_config_data);
    break;
    case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
        capture(image_window, sensor_config_data);
    break;
    case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
        normal_video(image_window, sensor_config_data);
    	break;
    default:
        //LOG_INF("[odin]default mode\n");
        preview(image_window, sensor_config_data);
        return ERROR_INVALID_SCENARIO_ID;
    }
    return ERROR_NONE;
}    /* control() */

static kal_uint32 set_video_mode(UINT16 framerate)
{
    /*This Function not used after ROME*/
    LOG_INF("framerate = %d\n ", framerate);
    /* SetVideoMode Function should fix framerate */
    /***********
     *if (framerate == 0)	 //Dynamic frame rate
     *	return ERROR_NONE;
     *spin_lock(&imgsensor_drv_lock);
     *if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
     *	imgsensor.current_fps = 296;
     *else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
     *	imgsensor.current_fps = 146;
     *else
     *	imgsensor.current_fps = framerate;
     *spin_unlock(&imgsensor_drv_lock);
     *set_max_framerate(imgsensor.current_fps, 1);
     ********/
    return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate)
{
    LOG_INF("enable = %d, framerate = %d ", enable, framerate);
    spin_lock(&imgsensor_drv_lock);
    if (enable) //enable auto flicker
    	imgsensor.autoflicker_en = KAL_TRUE;
    else //Cancel Auto flick
    	imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}

static kal_uint32 set_max_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate)
{
    kal_uint32 frame_length;

//    LOG_INF("scenario_id = %d, framerate = %d\n", scenario_id, framerate);

    switch (scenario_id) {
    case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
    	frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
    	spin_lock(&imgsensor_drv_lock);
    	imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ?
    		(frame_length - imgsensor_info.pre.framelength) : 0;
    	imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
    	imgsensor.min_frame_length = imgsensor.frame_length;
    	spin_unlock(&imgsensor_drv_lock);
    	if (imgsensor.frame_length > imgsensor.shutter)
    		set_dummy();
    	break;
    case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
    	if (framerate == 0)
    		return ERROR_NONE;
    	frame_length = imgsensor_info.normal_video.pclk / framerate * 10 /
    		imgsensor_info.normal_video.linelength;
    	spin_lock(&imgsensor_drv_lock);
    	imgsensor.dummy_line = (frame_length > imgsensor_info.normal_video.framelength) ?
    		(frame_length - imgsensor_info.normal_video.framelength) : 0;
    	imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
    	imgsensor.min_frame_length = imgsensor.frame_length;
    	spin_unlock(&imgsensor_drv_lock);
    	if (imgsensor.frame_length > imgsensor.shutter)
    		set_dummy();
    	break;
    case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
    	frame_length = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
    	spin_lock(&imgsensor_drv_lock);
    	imgsensor.dummy_line = (frame_length > imgsensor_info.cap.framelength) ?
    		(frame_length - imgsensor_info.cap.framelength) : 0;
    	imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
    	imgsensor.min_frame_length = imgsensor.frame_length;
    	spin_unlock(&imgsensor_drv_lock);
    	if (imgsensor.frame_length > imgsensor.shutter)
    		set_dummy();
    break;
    default:  //coding with  preview scenario by default
    	frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
    	spin_lock(&imgsensor_drv_lock);
    	imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ?
    		(frame_length - imgsensor_info.pre.framelength) : 0;
    	imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
    	imgsensor.min_frame_length = imgsensor.frame_length;
    	spin_unlock(&imgsensor_drv_lock);
    	if (imgsensor.frame_length > imgsensor.shutter)
    		set_dummy();
    	LOG_INF("error scenario_id = %d, we use preview scenario\n", scenario_id);
    	break;
    }
    return ERROR_NONE;
}

static kal_uint32 get_default_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
    LOG_INF("scenario_id = %d\n", scenario_id);

    switch (scenario_id) {
    case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
    	*framerate = imgsensor_info.pre.max_framerate;
    	break;
    case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
    	*framerate = imgsensor_info.normal_video.max_framerate;
    	break;
    case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
    	*framerate = imgsensor_info.cap.max_framerate;
    	break;
    default:
        break;
    }
    return ERROR_NONE;
}

static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
    LOG_INF("enable: %d\n", enable);

    if(enable)
    {
    	write_cmos_sensor(0xfd,0x03);
    	write_cmos_sensor(0x8c,0x00);
    	write_cmos_sensor(0x8e,0x00);
    	write_cmos_sensor(0x90,0x00);
    	write_cmos_sensor(0x92,0x00);
    	write_cmos_sensor(0x9b,0x00);
    	write_cmos_sensor(0xfe,0x02);
    }
    else
    {
    	write_cmos_sensor(0xfd,0x03);
    	write_cmos_sensor(0x81,0x00);
    }

    spin_lock(&imgsensor_drv_lock);
    imgsensor.test_pattern = enable;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}

static kal_uint32 streaming_control(kal_bool enable)
{
    //LOG_INF("streaming_control enable =%d\n", enable);
    if (enable){
    	write_cmos_sensor(0xfd, 0x03);
    	write_cmos_sensor(0xc2, 0x01);

    }else{
    	write_cmos_sensor(0xfd, 0x03);
    	write_cmos_sensor(0xc2, 0x00);
    }
    mdelay(10);

    return ERROR_NONE;
}



static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
    	UINT8 *feature_para,UINT32 *feature_para_len)
{
    UINT16 *feature_return_para_16 = (UINT16 *)feature_para;
    UINT16 *feature_data_16 = (UINT16 *)feature_para;
    UINT32 *feature_return_para_32 = (UINT32 *)feature_para;
    UINT32 *feature_data_32 = (UINT32 *)feature_para;
    unsigned long long *feature_data = (unsigned long long *)feature_para;

    struct SENSOR_WINSIZE_INFO_STRUCT *wininfo;
    MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data = (MSDK_SENSOR_REG_INFO_STRUCT *)feature_para;

    LOG_INF("feature_id = %d\n", feature_id);
    switch (feature_id) {
    case SENSOR_FEATURE_GET_PERIOD:
    	*feature_return_para_16++ = imgsensor.line_length;
    	*feature_return_para_16 = imgsensor.frame_length;
    	*feature_para_len=4;
    	break;
    case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
        *feature_return_para_32 = imgsensor.pclk;
        *feature_para_len = 4;
    break;
    case SENSOR_FEATURE_GET_MIPI_PIXEL_RATE:
    	{
    		kal_uint32 rate;

    		switch (*feature_data) {
    		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
    			rate = imgsensor_info.cap.mipi_pixel_rate;
    			break;
    		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
    			rate = imgsensor_info.normal_video.mipi_pixel_rate;
    			break;
    		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
    		default:
    			rate = imgsensor_info.pre.mipi_pixel_rate;
    			break;
    		}
    		*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = rate;
    	}
    	break;
    case SENSOR_FEATURE_SET_ESHUTTER:
    	set_shutter(*feature_data);
    	break;
    case SENSOR_FEATURE_SET_NIGHTMODE:
    	night_mode((BOOL)*feature_data);
    	break;
    case SENSOR_FEATURE_SET_GAIN:
    	set_gain((UINT16) *feature_data);
    	break;
    case SENSOR_FEATURE_SET_FLASHLIGHT:
    	break;
    case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
    	break;
    case SENSOR_FEATURE_SET_REGISTER:
    	write_cmos_sensor(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
    	break;
    case SENSOR_FEATURE_GET_REGISTER:
    	sensor_reg_data->RegData = read_cmos_sensor(sensor_reg_data->RegAddr);
    	LOG_INF("adb_i2c_read 0x%x = 0x%x\n", sensor_reg_data->RegAddr, sensor_reg_data->RegData);
    	break;
    case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
        *feature_return_para_32 = LENS_DRIVER_ID_DO_NOT_CARE;
        *feature_para_len = 4;
    break;
    case SENSOR_FEATURE_SET_VIDEO_MODE:
    	set_video_mode(*feature_data);
    	break;
    case SENSOR_FEATURE_CHECK_SENSOR_ID:
    	get_imgsensor_id(feature_return_para_32);
    	break;
    case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
    	set_auto_flicker_mode((BOOL)*feature_data_16, *(feature_data_16 + 1));
    	break;
    case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
    	set_max_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*feature_data, *(feature_data+1));
    	break;
    case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
    	get_default_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*(feature_data),
    		(MUINT32 *)(uintptr_t)(*(feature_data + 1)));
    	break;
    case SENSOR_FEATURE_SET_TEST_PATTERN:
    	set_test_pattern_mode((BOOL)*feature_data);
    	break;
    case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE: //for factory mode auto testing
    	*feature_return_para_32 = imgsensor_info.checksum_value;
    	*feature_para_len=4;
    	break;
    case SENSOR_FEATURE_SET_FRAMERATE:
        LOG_INF("current fps :%d\n", (UINT32)*feature_data);
        spin_lock(&imgsensor_drv_lock);
        imgsensor.current_fps = *feature_data;
        spin_unlock(&imgsensor_drv_lock);
    break;
    case SENSOR_FEATURE_SET_SHUTTER_FRAME_TIME:
    	set_shutter_frame_length((UINT16) *feature_data,
    		(UINT16) *(feature_data + 1));
    	break;
    case SENSOR_FEATURE_SET_STREAMING_SUSPEND:
    	streaming_control(KAL_FALSE);
    	break;
    case SENSOR_FEATURE_SET_STREAMING_RESUME:
    	if (*feature_data != 0)
    		set_shutter(*feature_data);
    	streaming_control(KAL_TRUE);
    	break;
    case SENSOR_FEATURE_SET_HDR:
        LOG_INF("ihdr enable :%d\n", (BOOL)*feature_data);
        spin_lock(&imgsensor_drv_lock);
        imgsensor.ihdr_en = (BOOL)*feature_data;
        spin_unlock(&imgsensor_drv_lock);
    break;
    case SENSOR_FEATURE_GET_CROP_INFO:
    	LOG_INF("SENSOR_FEATURE_GET_CROP_INFO scenarioId:%d\n", (UINT32)*feature_data);
    	wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT *)(uintptr_t)(*(feature_data + 1));
    	switch (*feature_data_32) {
    	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
    		memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[1], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
    		break;
    	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
    		memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[2], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
    		break;
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
        default:
    		memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[0], sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
    		break;
    	}
        break;
    case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
    	LOG_INF("SENSOR_SET_SENSOR_IHDR LE = %d, SE = %d, Gain = %d\n",
    		(UINT16)*feature_data, (UINT16)*(feature_data + 1), (UINT16)*(feature_data + 2));
    	ihdr_write_shutter_gain((UINT16)*feature_data, (UINT16)*(feature_data + 1),
    		(UINT16)*(feature_data + 2));
    	break;
    default:
    	break;
    }

    return ERROR_NONE;
}    /*    feature_control()  */

static struct SENSOR_FUNCTION_STRUCT sensor_func = {
    open,
    get_info,
    get_resolution,
    feature_control,
    control,
    close
};

UINT32 MOT_MAUI_OV02B10_MIPI_RAW_SensorInit(struct SENSOR_FUNCTION_STRUCT **pfFunc)
{
    /* To Do : Check Sensor status here */
    if (pfFunc!=NULL)
    	*pfFunc=&sensor_func;
    return ERROR_NONE;
}
