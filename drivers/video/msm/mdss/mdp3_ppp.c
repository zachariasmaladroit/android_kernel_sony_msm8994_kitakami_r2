/* Copyright (c) 2007, 2013-2015 The Linux Foundation. All rights reserved.
 * Copyright (C) 2007 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/file.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/sync.h>
#include <linux/sw_sync.h>
#include "linux/proc_fs.h"
#include <linux/delay.h>

#include "mdss_fb.h"
#include "mdp3_ppp.h"
#include "mdp3_hwio.h"
#include "mdp3.h"
#include "mdss_debug.h"

#define MDP_IS_IMGTYPE_BAD(x) ((x) >= MDP_IMGTYPE_LIMIT)
#define MDP_RELEASE_BW_TIMEOUT 50

#define MDP_PPP_MAX_BPP 4
#define MDP_PPP_DYNAMIC_FACTOR 3
#define MDP_PPP_MAX_READ_WRITE 3
#define ENABLE_SOLID_FILL	0x2
#define DISABLE_SOLID_FILL	0x0

struct ppp_resource ppp_res;

static const bool valid_fmt[MDP_IMGTYPE_LIMIT] = {
	[MDP_RGB_565] = true,
	[MDP_BGR_565] = true,
	[MDP_RGB_888] = true,
	[MDP_BGR_888] = true,
	[MDP_BGRA_8888] = true,
	[MDP_RGBA_8888] = true,
	[MDP_ARGB_8888] = true,
	[MDP_XRGB_8888] = true,
	[MDP_RGBX_8888] = true,
	[MDP_Y_CRCB_H2V2] = true,
	[MDP_Y_CBCR_H2V2] = true,
	[MDP_Y_CBCR_H2V2_ADRENO] = true,
	[MDP_Y_CBCR_H2V2_VENUS] = true,
	[MDP_YCRYCB_H2V1] = true,
	[MDP_Y_CBCR_H2V1] = true,
	[MDP_Y_CRCB_H2V1] = true,
	[MDP_BGRX_8888] = true,
};

#define MAX_LIST_WINDOW 16
#define MDP3_PPP_MAX_LIST_REQ 8

struct blit_req_list {
	int count;
	struct mdp_blit_req req_list[MAX_LIST_WINDOW];
	struct mdp3_img_data src_data[MAX_LIST_WINDOW];
	struct mdp3_img_data dst_data[MAX_LIST_WINDOW];
	struct sync_fence *acq_fen[MDP_MAX_FENCE_FD];
	u32 acq_fen_cnt;
	int cur_rel_fen_fd;
	struct sync_pt *cur_rel_sync_pt;
	struct sync_fence *cur_rel_fence;
	struct sync_fence *last_rel_fence;
};

struct blit_req_queue {
	struct blit_req_list req[MDP3_PPP_MAX_LIST_REQ];
	int count;
	int push_idx;
	int pop_idx;
};

struct ppp_status {
	bool wait_for_pop;
	struct completion ppp_comp;
	struct completion pop_q_comp;
	struct mutex req_mutex; /* Protect request queue */
	struct mutex config_ppp_mutex; /* Only one client configure register */
	struct msm_fb_data_type *mfd;

	struct kthread_work blit_work;
	struct kthread_worker kworker;
	struct task_struct *blit_thread;
	struct blit_req_queue req_q;

	struct sw_sync_timeline *timeline;
	int timeline_value;

	struct timer_list free_bw_timer;
	struct work_struct free_bw_work;
	bool bw_update;
	bool bw_on;
	u32 mdp_clk;
};

static struct ppp_status *ppp_stat;


int ppp_get_bpp(uint32_t format, uint32_t fb_format)
{
	int bpp = -EINVAL;
	if (format == MDP_FB_FORMAT)
		format = fb_format;

	bpp = ppp_bpp(format);
	if (bpp <= 0)
		pr_err("%s incorrect format %d\n", __func__, format);
	return bpp;
}

int mdp3_ppp_get_img(struct mdp_img *img, struct mdp_blit_req *req,
		struct mdp3_img_data *data)
{
	struct msmfb_data fb_data;
	uint32_t stride;
	int bpp = ppp_bpp(img->format);

	if (bpp <= 0) {
		pr_err("%s incorrect format %d\n", __func__, img->format);
		return -EINVAL;
	}

	fb_data.flags = img->priv;
	fb_data.memory_id = img->memory_id;
	fb_data.offset = 0;

	stride = img->width * bpp;
	data->padding = 16 * stride;

	return mdp3_get_img(&fb_data, data, MDP3_CLIENT_PPP);
}

/* Check format */
int mdp3_ppp_verify_fmt(struct mdp_blit_req *req)
{
	if (MDP_IS_IMGTYPE_BAD(req->src.format) ||
	    MDP_IS_IMGTYPE_BAD(req->dst.format)) {
		pr_err("%s: Color format out of range\n", __func__);
		return -EINVAL;
	}

	if (!valid_fmt[req->src.format] ||
	    !valid_fmt[req->dst.format]) {
		pr_err("%s: Color format not supported\n", __func__);
		return -EINVAL;
	}
	return 0;
}

/* Check resolution */
int mdp3_ppp_verify_res(struct mdp_blit_req *req)
{
	if ((req->src.width == 0) || (req->src.height == 0) ||
	    (req->src_rect.w == 0) || (req->src_rect.h == 0) ||
	    (req->dst.width == 0) || (req->dst.height == 0) ||
	    (req->dst_rect.w == 0) || (req->dst_rect.h == 0)) {
		pr_err("%s: Height/width can't be 0\n", __func__);
		return -EINVAL;
	}

	if (((req->src_rect.x + req->src_rect.w) > req->src.width) ||
	    ((req->src_rect.y + req->src_rect.h) > req->src.height)) {
		pr_err("%s: src roi larger than boundary\n", __func__);
		return -EINVAL;
	}

	if (((req->dst_rect.x + req->dst_rect.w) > req->dst.width) ||
	    ((req->dst_rect.y + req->dst_rect.h) > req->dst.height)) {
		pr_err("%s: dst roi larger than boundary\n", __func__);
		return -EINVAL;
	}
	return 0;
}

/* scaling range check */
int mdp3_ppp_verify_scale(struct mdp_blit_req *req)
{
	u32 src_width, src_height, dst_width, dst_height;

	src_width = req->src_rect.w;
	src_height = req->src_rect.h;

	if (req->flags & MDP_ROT_90) {
		dst_width = req->dst_rect.h;
		dst_height = req->dst_rect.w;
	} else {
		dst_width = req->dst_rect.w;
		dst_height = req->dst_rect.h;
	}

	switch (req->dst.format) {
	case MDP_Y_CRCB_H2V2:
	case MDP_Y_CBCR_H2V2:
		src_width = (src_width / 2) * 2;
		src_height = (src_height / 2) * 2;
		dst_width = (dst_width / 2) * 2;
		dst_height = (dst_height / 2) * 2;
		break;

	case MDP_Y_CRCB_H2V1:
	case MDP_Y_CBCR_H2V1:
	case MDP_YCRYCB_H2V1:
		src_width = (src_width / 2) * 2;
		dst_width = (dst_width / 2) * 2;
		break;

	default:
		break;
	}

	if (((MDP_SCALE_Q_FACTOR * dst_width) / src_width >
	     MDP_MAX_X_SCALE_FACTOR)
	    || ((MDP_SCALE_Q_FACTOR * dst_width) / src_width <
		MDP_MIN_X_SCALE_FACTOR)) {
		pr_err("%s: x req scale factor beyond capability\n", __func__);
		return -EINVAL;
	}

	if (((MDP_SCALE_Q_FACTOR * dst_height) / src_height >
	     MDP_MAX_Y_SCALE_FACTOR)
	    || ((MDP_SCALE_Q_FACTOR * dst_height) / src_height <
		MDP_MIN_Y_SCALE_FACTOR)) {
		pr_err("%s: y req scale factor beyond capability\n", __func__);
		return -EINVAL;
	}
	return 0;
}

/* operation check */
int mdp3_ppp_verify_op(struct mdp_blit_req *req)
{
	/*
	 * MDP_DEINTERLACE & MDP_SHARPENING Flags are not valid for MDP3
	 * so using them together for MDP_SMART_BLIT.
	 */
	if ((req->flags & MDP_SMART_BLIT) == MDP_SMART_BLIT)
		return 0;
	if (req->flags & MDP_DEINTERLACE) {
		pr_err("\n%s(): deinterlace not supported", __func__);
		return -EINVAL;
	}

	if (req->flags & MDP_SHARPENING) {
		pr_err("\n%s(): sharpening not supported", __func__);
		return -EINVAL;
	}
	return 0;
}

int mdp3_ppp_verify_req(struct mdp_blit_req *req)
{
	int rc;

	if (req == NULL) {
		pr_err("%s: req == null\n", __func__);
		return -EINVAL;
	}

	rc = mdp3_ppp_verify_fmt(req);
	rc |= mdp3_ppp_verify_res(req);
	rc |= mdp3_ppp_verify_scale(req);
	rc |= mdp3_ppp_verify_op(req);

	return rc;
}

int mdp3_ppp_pipe_wait(void)
{
	int ret = 1;

	/*
	 * wait 200 ms for ppp operation to complete before declaring
	 * the MDP hung
	 */
	ret = wait_for_completion_timeout(
	  &ppp_stat->ppp_comp, msecs_to_jiffies(200));
	if (!ret)
		pr_err("%s: Timed out waiting for the MDP.\n",
			__func__);

	return ret;
}

uint32_t mdp3_calc_tpval(struct ppp_img_desc *img, uint32_t old_tp)
{
	uint32_t tpVal;
	uint8_t plane_tp;

	tpVal = 0;
	if ((img->color_fmt == MDP_RGB_565)
	    || (img->color_fmt == MDP_BGR_565)) {
		/* transparent color conversion into 24 bpp */
		plane_tp = (uint8_t) ((old_tp & 0xF800) >> 11);
		tpVal |= ((plane_tp << 3) | ((plane_tp & 0x1C) >> 2)) << 16;
		plane_tp = (uint8_t) (old_tp & 0x1F);
		tpVal |= ((plane_tp << 3) | ((plane_tp & 0x1C) >> 2)) << 8;

		plane_tp = (uint8_t) ((old_tp & 0x7E0) >> 5);
		tpVal |= ((plane_tp << 2) | ((plane_tp & 0x30) >> 4));
	} else {
		/* 24bit RGB to RBG conversion */
		tpVal = (old_tp & 0xFF00) >> 8;
		tpVal |= (old_tp & 0xFF) << 8;
		tpVal |= (old_tp & 0xFF0000);
	}

	return tpVal;
}

static void mdp3_ppp_intr_handler(int type, void *arg)
{
	complete(&ppp_stat->ppp_comp);
}

static int mdp3_ppp_callback_setup(void)
{
	int rc;
	struct mdp3_intr_cb ppp_done_cb = {
		.cb = mdp3_ppp_intr_handler,
		.data = NULL,
	};

	rc = mdp3_set_intr_callback(MDP3_PPP_DONE, &ppp_done_cb);
	return rc;
}

void mdp3_ppp_kickoff(void)
{
	init_completion(&ppp_stat->ppp_comp);
	mdp3_irq_enable(MDP3_PPP_DONE);
	ppp_enable();
	ATRACE_BEGIN("mdp3_wait_for_ppp_comp");
	mdp3_ppp_pipe_wait();
	ATRACE_END("mdp3_wait_for_ppp_comp");
	mdp3_irq_disable(MDP3_PPP_DONE);
}

u32 mdp3_clk_calc(struct msm_fb_data_type *mfd, struct blit_req_list *lreq)
{
	struct mdss_panel_info *panel_info = mfd->panel_info;
	int i, lcount = 0;
	struct mdp_blit_req *req;
	u32 total_pixel;
	u32 mdp_clk_rate = MDP_CORE_CLK_RATE_SVS;

	total_pixel = panel_info->xres * panel_info->yres;
	if (total_pixel > SVS_MAX_PIXEL)
		return MDP_CORE_CLK_RATE_MAX;

	for (i = 0; i < lcount; i++) {
		req = &(lreq->req_list[i]);

		if (req->src_rect.h != req->dst_rect.h ||
				req->src_rect.w != req->dst_rect.w) {
			mdp_clk_rate = MDP_CORE_CLK_RATE_MAX;
			break;
		}
	}
	return mdp_clk_rate;
}

struct bpp_info {
	int bpp_num;
	int bpp_den;
	int bpp_pln;
};

int mdp3_get_bpp_info(int format, struct bpp_info *bpp)
{
	int rc = 0;

	switch (format) {
	case MDP_RGB_565:
	case MDP_BGR_565:
		bpp->bpp_num = 2;
		bpp->bpp_den = 1;
		bpp->bpp_pln = 2;
		break;
	case MDP_RGB_888:
	case MDP_BGR_888:
		bpp->bpp_num = 3;
		bpp->bpp_den = 1;
		bpp->bpp_pln = 3;
		break;
	case MDP_BGRA_8888:
	case MDP_RGBA_8888:
	case MDP_ARGB_8888:
	case MDP_XRGB_8888:
	case MDP_RGBX_8888:
	case MDP_BGRX_8888:
		bpp->bpp_num = 4;
		bpp->bpp_den = 1;
		bpp->bpp_pln = 4;
		break;
	case MDP_Y_CRCB_H2V2:
	case MDP_Y_CBCR_H2V2:
	case MDP_Y_CBCR_H2V2_ADRENO:
	case MDP_Y_CBCR_H2V2_VENUS:
		bpp->bpp_num = 3;
		bpp->bpp_den = 2;
		bpp->bpp_pln = 1;
		break;
	case MDP_Y_CBCR_H2V1:
	case MDP_Y_CRCB_H2V1:
		bpp->bpp_num = 2;
		bpp->bpp_den = 1;
		bpp->bpp_pln = 1;
		break;
	case MDP_YCRYCB_H2V1:
		bpp->bpp_num = 2;
		bpp->bpp_den = 1;
		bpp->bpp_pln = 2;
		break;
	default:
		rc = -EINVAL;
	}
	return rc;
}

u64 mdp3_adjust_scale_factor(struct mdp_blit_req *req, u32 bw_req, int bpp)
{
	int src_h, src_w;
	int dst_h, dst_w;

	src_h = req->src_rect.h;
	src_w = req->src_rect.w;

	dst_h = req->dst_rect.h;
	dst_w = req->dst_rect.w;

	if ((!(req->flags & MDP_ROT_90) && src_h == dst_h && src_w == dst_w) ||
		((req->flags & MDP_ROT_90) && src_h == dst_w && src_w == dst_h))
		return bw_req;

	bw_req = (bw_req + (bw_req * dst_h) / (4 * src_h));
	bw_req = (bw_req + (bw_req * dst_w) / (4 * src_w) +
			(bw_req * dst_w) / (bpp * src_w));

	return bw_req;
}

int mdp3_calc_ppp_res(struct msm_fb_data_type *mfd,  struct blit_req_list *lreq)
{
	struct mdss_panel_info *panel_info = mfd->panel_info;
	int i, lcount = 0;
	struct mdp_blit_req *req;
	struct bpp_info bpp;
	u32 src_read_bw = 0;
	u32 bg_read_bw = 0;
	u32 dst_write_bw = 0;
	u64 honest_ppp_ab = 0;
	u32 fps = 0;
	int smart_blit_fg_indx = -1;
	u32 smart_blit_bg_read_bw = 0;

	ATRACE_BEGIN(__func__);
	lcount = lreq->count;
	if (lcount == 0) {
		pr_err("Blit with request count 0, continue to recover!!!\n");
		ATRACE_END(__func__);
		return 0;
	}
	if (lreq->req_list[0].flags & MDP_SOLID_FILL) {
		/* Do not update BW for solid fill */
		ATRACE_END(__func__);
		return 0;
	}

	for (i = 0; i < lcount; i++) {
		req = &(lreq->req_list[i]);

		if (req->fps > 0 && req->fps <= panel_info->mipi.frame_rate) {
			if (fps == 0)
				fps = req->fps;
			else
				fps = panel_info->mipi.frame_rate;
		}

		mdp3_get_bpp_info(req->src.format, &bpp);

		if ((bpp.bpp_pln == 1 || req->src.format == MDP_YCRYCB_H2V1) &&
			req->src_rect.w >= 1280 && req->src_rect.h >= 720) {
			/* Above 720p only 30fps video plaback is supported */
			fps = 30;
		} else {
			/**
			 * Set FPS to mipi rate as currently there is
			 * no way to get this
			 */
			fps = panel_info->mipi.frame_rate;
		}

		if (lreq->req_list[i].flags & MDP_SMART_BLIT) {
			/*
			 * Flag for smart blit FG layer index
			 * If blit request at index "n" has
			 * MDP_SMART_BLIT flag set then it will be used as BG
			 * layer in smart blit and request at index "n+1"
			 * will be used as FG layer
			 */
			smart_blit_fg_indx = i + 1;
			bg_read_bw = req->src_rect.w * req->src_rect.h *
						bpp.bpp_num / bpp.bpp_den;
			bg_read_bw = mdp3_adjust_scale_factor(req,
						bg_read_bw, bpp.bpp_pln);
			/* Cache read BW of smart blit BG layer */
			smart_blit_bg_read_bw = bg_read_bw;
		} else {
			src_read_bw = req->src_rect.w * req->src_rect.h *
						bpp.bpp_num / bpp.bpp_den;
			src_read_bw = mdp3_adjust_scale_factor(req,
						src_read_bw, bpp.bpp_pln);

			mdp3_get_bpp_info(req->dst.format, &bpp);

			if (smart_blit_fg_indx == i) {
				bg_read_bw = smart_blit_bg_read_bw;
				smart_blit_fg_indx = -1;
			} else {
				if ((req->transp_mask != MDP_TRANSP_NOP) ||
					(req->alpha < MDP_ALPHA_NOP) ||
					(req->src.format == MDP_ARGB_8888) ||
					(req->src.format == MDP_BGRA_8888) ||
					(req->src.format == MDP_RGBA_8888)) {
					bg_read_bw = req->dst_rect.w * req->dst_rect.h *
								bpp.bpp_num / bpp.bpp_den;
					bg_read_bw = mdp3_adjust_scale_factor(req,
								bg_read_bw, bpp.bpp_pln);
				} else {
					bg_read_bw = 0;
				}
			}
			dst_write_bw = req->dst_rect.w * req->dst_rect.h *
						bpp.bpp_num / bpp.bpp_den;
			honest_ppp_ab += (src_read_bw + bg_read_bw + dst_write_bw);
                }
	}

	if (fps != 0)
		honest_ppp_ab = honest_ppp_ab * fps;
	else
		honest_ppp_ab = honest_ppp_ab * panel_info->mipi.frame_rate;

	if (honest_ppp_ab != ppp_res.next_ab) {
		pr_debug("bandwidth vote update for ppp: ab = %llx\n",
								honest_ppp_ab);
		ppp_res.next_ab = honest_ppp_ab;
		ppp_res.next_ib = honest_ppp_ab;
		ppp_stat->bw_update = true;
		ATRACE_INT("mdp3_ppp_bus_quota", honest_ppp_ab);
	}
	ppp_res.clk_rate = mdp3_clk_calc(mfd, lreq);
	ATRACE_INT("mdp3_ppp_clk_rate", ppp_res.clk_rate);
	ATRACE_END(__func__);
	return 0;
}

int mdp3_ppp_turnon(struct msm_fb_data_type *mfd, int on_off)
{
	uint64_t ab = 0, ib = 0;
	int rate = 0;
	int rc;

	if (on_off) {
		rate = ppp_res.clk_rate;
		ab = ppp_res.next_ab;
		ib = ppp_res.next_ib;
	}
	mdp3_clk_set_rate(MDP3_CLK_MDP_SRC, rate, MDP3_CLIENT_PPP);
	rc = mdp3_res_update(on_off, 0, MDP3_CLIENT_PPP);
	if (rc < 0) {
		pr_err("%s: mdp3_clk_enable failed\n", __func__);
		return rc;
	}
	rc = mdp3_bus_scale_set_quota(MDP3_CLIENT_PPP, ab, ib);
	if (rc < 0) {
		mdp3_res_update(!on_off, 0, MDP3_CLIENT_PPP);
		pr_err("%s: scale_set_quota failed\n", __func__);
		return rc;
	}
	ppp_stat->bw_on = on_off;
	ppp_stat->mdp_clk = MDP_CORE_CLK_RATE_SVS;
	ppp_stat->bw_update = false;
	return 0;
}

void mdp3_start_ppp(struct ppp_blit_op *blit_op)
{
	/* Wait for the pipe to clear */
	if (MDP3_REG_READ(MDP3_REG_DISPLAY_STATUS) &
			MDP3_PPP_ACTIVE) {
		pr_err("ppp core is hung up on previous request\n");
		return;
	}
	config_ppp_op_mode(blit_op);
	if (blit_op->solid_fill) {
		MDP3_REG_WRITE(0x10138, 0x10000000);
		MDP3_REG_WRITE(0x1014c, 0xffffffff);
		MDP3_REG_WRITE(0x101b8, 0);
		MDP3_REG_WRITE(0x101bc, 0);
		MDP3_REG_WRITE(0x1013c, 0);
		MDP3_REG_WRITE(0x10140, 0);
		MDP3_REG_WRITE(0x10144, 0);
		MDP3_REG_WRITE(0x10148, 0);
		MDP3_REG_WRITE(MDP3_TFETCH_FILL_COLOR,
					blit_op->solid_fill_color);
		MDP3_REG_WRITE(MDP3_TFETCH_SOLID_FILL,
					ENABLE_SOLID_FILL);
	} else {
		MDP3_REG_WRITE(MDP3_TFETCH_SOLID_FILL,
					DISABLE_SOLID_FILL);
	}
	/* Skip PPP kickoff for SMART_BLIT BG layer */
	if (blit_op->mdp_op & MDPOP_SMART_BLIT)
		pr_debug("Skip mdp3_ppp_kickoff\n");
	else
		mdp3_ppp_kickoff();
}

static int solid_fill_workaround(struct mdp_blit_req *req,
						struct ppp_blit_op *blit_op)
{
	/* Make width 2 when there is a solid fill of width 1, and make
	sure width does not become zero while trying to avoid odd width */
	if (blit_op->dst.roi.width == 1) {
		if (req->dst_rect.x + 2 > req->dst.width) {
			pr_err("%s: Unable to handle solid fill of width 1",
								__func__);
			return -EINVAL;
		}
		blit_op->dst.roi.width = 2;
	}
	if (blit_op->src.roi.width == 1) {
		if (req->src_rect.x + 2 > req->src.width) {
			pr_err("%s: Unable to handle solid fill of width 1",
								__func__);
			return -EINVAL;
		}
		blit_op->src.roi.width = 2;
	}

	/* Avoid odd width, as it could hang ppp during solid fill */
	blit_op->dst.roi.width = (blit_op->dst.roi.width / 2) * 2;
	blit_op->src.roi.width = (blit_op->src.roi.width / 2) * 2;

	/* Avoid RGBA format, as it could hang ppp during solid fill */
	if (blit_op->src.color_fmt == MDP_RGBA_8888)
		blit_op->src.color_fmt = MDP_RGBX_8888;
	if (blit_op->dst.color_fmt == MDP_RGBA_8888)
		blit_op->dst.color_fmt = MDP_RGBX_8888;
	return 0;
}

static int mdp3_ppp_process_req(struct ppp_blit_op *blit_op,
	struct mdp_blit_req *req, struct mdp3_img_data *src_data,
	struct mdp3_img_data *dst_data)
{
	unsigned long srcp0_start, srcp0_len, dst_start, dst_len;
	uint32_t dst_width, dst_height;
	int ret = 0;

	srcp0_start = (unsigned long) src_data->addr;
	srcp0_len = (unsigned long) src_data->len;
	dst_start = (unsigned long) dst_data->addr;
	dst_len = (unsigned long) dst_data->len;

	blit_op->dst.prop.width = req->dst.width;
	blit_op->dst.prop.height = req->dst.height;

	blit_op->dst.color_fmt = req->dst.format;
	blit_op->dst.p0 = (void *) dst_start;
	blit_op->dst.p0 += req->dst.offset;

	blit_op->dst.roi.x = req->dst_rect.x;
	blit_op->dst.roi.y = req->dst_rect.y;
	blit_op->dst.roi.width = req->dst_rect.w;
	blit_op->dst.roi.height = req->dst_rect.h;

	blit_op->src.roi.x = req->src_rect.x;
	blit_op->src.roi.y = req->src_rect.y;
	blit_op->src.roi.width = req->src_rect.w;
	blit_op->src.roi.height = req->src_rect.h;

	blit_op->src.prop.width = req->src.width;
	blit_op->src.prop.height = req->src.height;
	blit_op->src.color_fmt = req->src.format;


	blit_op->src.p0 = (void *) (srcp0_start + req->src.offset);
	if (blit_op->src.color_fmt == MDP_Y_CBCR_H2V2_ADRENO)
		blit_op->src.p1 =
			(void *) ((uint32_t) blit_op->src.p0 +
				ALIGN((ALIGN(req->src.width, 32) *
				ALIGN(req->src.height, 32)), 4096));
	else if (blit_op->src.color_fmt == MDP_Y_CBCR_H2V2_VENUS)
		blit_op->src.p1 =
			(void *) ((uint32_t) blit_op->src.p0 +
				ALIGN((ALIGN(req->src.width, 128) *
				ALIGN(req->src.height, 32)), 4096));
	else
		blit_op->src.p1 = (void *) ((uint32_t) blit_op->src.p0 +
			req->src.width * req->src.height);

	if (req->flags & MDP_IS_FG)
		blit_op->mdp_op |= MDPOP_LAYER_IS_FG;

	/* blending check */
	if (req->transp_mask != MDP_TRANSP_NOP) {
		blit_op->mdp_op |= MDPOP_TRANSP;
		blit_op->blend.trans_color =
			mdp3_calc_tpval(&blit_op->src, req->transp_mask);
	} else {
		blit_op->blend.trans_color = 0;
	}

	req->alpha &= 0xff;
	if (req->alpha < MDP_ALPHA_NOP) {
		blit_op->mdp_op |= MDPOP_ALPHAB;
		blit_op->blend.const_alpha = req->alpha;
	} else {
		blit_op->blend.const_alpha = 0xff;
	}

	/* rotation check */
	if (req->flags & MDP_FLIP_LR)
		blit_op->mdp_op |= MDPOP_LR;
	if (req->flags & MDP_FLIP_UD)
		blit_op->mdp_op |= MDPOP_UD;
	if (req->flags & MDP_ROT_90)
		blit_op->mdp_op |= MDPOP_ROT90;
	if (req->flags & MDP_DITHER)
		blit_op->mdp_op |= MDPOP_DITHER;

	if (req->flags & MDP_BLEND_FG_PREMULT)
		blit_op->mdp_op |= MDPOP_FG_PM_ALPHA;

	/* scale check */
	if (req->flags & MDP_ROT_90) {
		dst_width = req->dst_rect.h;
		dst_height = req->dst_rect.w;
	} else {
		dst_width = req->dst_rect.w;
		dst_height = req->dst_rect.h;
	}

	if ((blit_op->src.roi.width != dst_width) ||
			(blit_op->src.roi.height != dst_height))
		blit_op->mdp_op |= MDPOP_ASCALE;

	if (req->flags & MDP_BLUR)
		blit_op->mdp_op |= MDPOP_ASCALE | MDPOP_BLUR;

	if (req->flags & MDP_SOLID_FILL) {
		ret = solid_fill_workaround(req, blit_op);
		if (ret)
			return ret;

		blit_op->solid_fill_color = (req->const_color.g & 0xFF)|
				(req->const_color.r & 0xFF) << 8 |
				(req->const_color.b & 0xFF)  << 16 |
				(req->const_color.alpha & 0xFF) << 24;
		blit_op->solid_fill = true;
	} else {
		blit_op->solid_fill = false;
	}

	if (req->flags & MDP_SMART_BLIT)
		blit_op->mdp_op |= MDPOP_SMART_BLIT;

	return ret;
}

static void mdp3_ppp_tile_workaround(struct ppp_blit_op *blit_op,
	struct mdp_blit_req *req)
{
	int dst_h, src_w, i;
	uint32_t mdp_op = blit_op->mdp_op;
	void *src_p0 = blit_op->src.p0;
	void *src_p1 = blit_op->src.p1;
	void *dst_p0 = blit_op->dst.p0;

	src_w = req->src_rect.w;
	dst_h = blit_op->dst.roi.height;
	/* bg tile fetching HW workaround */
	for (i = 0; i < (req->dst_rect.h / 16); i++) {
		/* this tile size */
		blit_op->dst.roi.height = 16;
		blit_op->src.roi.width =
			(16 * req->src_rect.w) / req->dst_rect.h;

		/* if it's out of scale range... */
		if (((MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
			 blit_op->src.roi.width) > MDP_MAX_X_SCALE_FACTOR)
			blit_op->src.roi.width =
				(MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
				MDP_MAX_X_SCALE_FACTOR;
		else if (((MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
			  blit_op->src.roi.width) < MDP_MIN_X_SCALE_FACTOR)
			blit_op->src.roi.width =
				(MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
				MDP_MIN_X_SCALE_FACTOR;

		mdp3_start_ppp(blit_op);

		/* next tile location */
		blit_op->dst.roi.y += 16;
		blit_op->src.roi.x += blit_op->src.roi.width;

		/* this is for a remainder update */
		dst_h -= 16;
		src_w -= blit_op->src.roi.width;
		/* restore parameters that may have been overwritten */
		blit_op->mdp_op = mdp_op;
		blit_op->src.p0 = src_p0;
		blit_op->src.p1 = src_p1;
		blit_op->dst.p0 = dst_p0;
	}

	if ((dst_h < 0) || (src_w < 0))
		pr_err
			("msm_fb: mdp_blt_ex() unexpected result! line:%d\n",
			 __LINE__);

	/* remainder update */
	if ((dst_h > 0) && (src_w > 0)) {
		u32 tmp_v;

		blit_op->dst.roi.height = dst_h;
		blit_op->src.roi.width = src_w;

		if (((MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
			 blit_op->src.roi.width) > MDP_MAX_X_SCALE_FACTOR) {
			tmp_v =
				(MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
				MDP_MAX_X_SCALE_FACTOR +
				((MDP_SCALE_Q_FACTOR *
				blit_op->dst.roi.height) %
				MDP_MAX_X_SCALE_FACTOR ? 1 : 0);

			/* move x location as roi width gets bigger */
			blit_op->src.roi.x -= tmp_v - blit_op->src.roi.width;
			blit_op->src.roi.width = tmp_v;
		} else if (((MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
			 blit_op->src.roi.width) < MDP_MIN_X_SCALE_FACTOR) {
			tmp_v =
				(MDP_SCALE_Q_FACTOR * blit_op->dst.roi.height) /
				MDP_MIN_X_SCALE_FACTOR +
				((MDP_SCALE_Q_FACTOR *
				blit_op->dst.roi.height) %
				MDP_MIN_X_SCALE_FACTOR ? 1 : 0);

			/*
			 * we don't move x location for continuity of
			 * source image
			 */
			blit_op->src.roi.width = tmp_v;
		}


		mdp3_start_ppp(blit_op);
	}
}

static int mdp3_ppp_blit(struct msm_fb_data_type *mfd,
	struct mdp_blit_req *req, struct mdp3_img_data *src_data,
	struct mdp3_img_data *dst_data)
{
	struct ppp_blit_op blit_op;
	int ret = 0;

	memset(&blit_op, 0, sizeof(blit_op));

	if (req->dst.format == MDP_FB_FORMAT)
		req->dst.format =  mfd->fb_imgType;
	if (req->src.format == MDP_FB_FORMAT)
		req->src.format = mfd->fb_imgType;

	if (mdp3_ppp_verify_req(req)) {
		pr_err("%s: invalid image!\n", __func__);
		return -EINVAL;
	}

	ret = mdp3_ppp_process_req(&blit_op, req, src_data, dst_data);
	if (ret) {
		pr_err("%s: Failed to process the blit request", __func__);
		return ret;
	}

	if (((blit_op.mdp_op & (MDPOP_TRANSP | MDPOP_ALPHAB)) ||
	     (req->src.format == MDP_ARGB_8888) ||
	     (req->src.format == MDP_BGRA_8888) ||
	     (req->src.format == MDP_RGBA_8888)) &&
	    (blit_op.mdp_op & MDPOP_ROT90) && (req->dst_rect.w <= 16)) {
		mdp3_ppp_tile_workaround(&blit_op, req);
	} else {
		mdp3_start_ppp(&blit_op);
	}

	return 0;
}

static int mdp3_ppp_blit_workaround(struct msm_fb_data_type *mfd,
		struct mdp_blit_req *req, unsigned int remainder,
		struct mdp3_img_data *src_data,
		struct mdp3_img_data *dst_data)
{
	int ret;
	struct mdp_blit_req splitreq;
	int s_x_0, s_x_1, s_w_0, s_w_1, s_y_0, s_y_1, s_h_0, s_h_1;
	int d_x_0, d_x_1, d_w_0, d_w_1, d_y_0, d_y_1, d_h_0, d_h_1;

	/* make new request as provide by user */
	splitreq = *req;

	/* break dest roi at width*/
	d_y_0 = d_y_1 = req->dst_rect.y;
	d_h_0 = d_h_1 = req->dst_rect.h;
	d_x_0 = req->dst_rect.x;

	if (remainder == 14 || remainder == 6)
		d_w_1 = req->dst_rect.w / 2;
	else
		d_w_1 = (req->dst_rect.w - 1) / 2 - 1;

	d_w_0 = req->dst_rect.w - d_w_1;
	d_x_1 = d_x_0 + d_w_0;
	/* blit first region */
	if (((splitreq.flags & 0x07) == 0x07) ||
		((splitreq.flags & 0x07) == 0x05) ||
		((splitreq.flags & 0x07) == 0x02) ||
		((splitreq.flags & 0x07) == 0x0)) {

		if (splitreq.flags & MDP_ROT_90) {
			s_x_0 = s_x_1 = req->src_rect.x;
			s_w_0 = s_w_1 = req->src_rect.w;
			s_y_0 = req->src_rect.y;
			s_h_1 = (req->src_rect.h * d_w_1) /
				req->dst_rect.w;
			s_h_0 = req->src_rect.h - s_h_1;
			s_y_1 = s_y_0 + s_h_0;
			if (d_w_1 >= 8 * s_h_1) {
				s_h_1++;
				s_y_1--;
			}
		} else {
			s_y_0 = s_y_1 = req->src_rect.y;
			s_h_0 = s_h_1 = req->src_rect.h;
			s_x_0 = req->src_rect.x;
			s_w_1 = (req->src_rect.w * d_w_1) /
				req->dst_rect.w;
			s_w_0 = req->src_rect.w - s_w_1;
			s_x_1 = s_x_0 + s_w_0;
			if (d_w_1 >= 8 * s_w_1) {
				s_w_1++;
				s_x_1--;
			}
		}

		splitreq.src_rect.h = s_h_0;
		splitreq.src_rect.y = s_y_0;
		splitreq.dst_rect.h = d_h_0;
		splitreq.dst_rect.y = d_y_0;
		splitreq.src_rect.x = s_x_0;
		splitreq.src_rect.w = s_w_0;
		splitreq.dst_rect.x = d_x_0;
		splitreq.dst_rect.w = d_w_0;
	} else {
		if (splitreq.flags & MDP_ROT_90) {
			s_x_0 = s_x_1 = req->src_rect.x;
			s_w_0 = s_w_1 = req->src_rect.w;
			s_y_0 = req->src_rect.y;
			s_h_1 = (req->src_rect.h * d_w_0) /
				req->dst_rect.w;
			s_h_0 = req->src_rect.h - s_h_1;
			s_y_1 = s_y_0 + s_h_0;
			if (d_w_0 >= 8 * s_h_1) {
				s_h_1++;
				s_y_1--;
			}
		} else {
			s_y_0 = s_y_1 = req->src_rect.y;
			s_h_0 = s_h_1 = req->src_rect.h;
			s_x_0 = req->src_rect.x;
			s_w_1 = (req->src_rect.w * d_w_0) /
				req->dst_rect.w;
			s_w_0 = req->src_rect.w - s_w_1;
			s_x_1 = s_x_0 + s_w_0;
			if (d_w_0 >= 8 * s_w_1) {
				s_w_1++;
				s_x_1--;
			}
		}
		splitreq.src_rect.h = s_h_0;
		splitreq.src_rect.y = s_y_0;
		splitreq.dst_rect.h = d_h_1;
		splitreq.dst_rect.y = d_y_1;
		splitreq.src_rect.x = s_x_0;
		splitreq.src_rect.w = s_w_0;
		splitreq.dst_rect.x = d_x_1;
		splitreq.dst_rect.w = d_w_1;
	}

	/* No need to split in height */
	ret = mdp3_ppp_blit(mfd, &splitreq, src_data, dst_data);

	if (ret)
		return ret;
	/* blit second region */
	if (((splitreq.flags & 0x07) == 0x07) ||
		((splitreq.flags & 0x07) == 0x05) ||
		((splitreq.flags & 0x07) == 0x02) ||
		((splitreq.flags & 0x07) == 0x0)) {
		splitreq.src_rect.h = s_h_1;
		splitreq.src_rect.y = s_y_1;
		splitreq.dst_rect.h = d_h_1;
		splitreq.dst_rect.y = d_y_1;
		splitreq.src_rect.x = s_x_1;
		splitreq.src_rect.w = s_w_1;
		splitreq.dst_rect.x = d_x_1;
		splitreq.dst_rect.w = d_w_1;
	} else {
		splitreq.src_rect.h = s_h_1;
		splitreq.src_rect.y = s_y_1;
		splitreq.dst_rect.h = d_h_0;
		splitreq.dst_rect.y = d_y_0;
		splitreq.src_rect.x = s_x_1;
		splitreq.src_rect.w = s_w_1;
		splitreq.dst_rect.x = d_x_0;
		splitreq.dst_rect.w = d_w_0;
	}

	/* No need to split in height ... just width */
	return mdp3_ppp_blit(mfd, &splitreq, src_data, dst_data);
}

int mdp3_ppp_start_blit(struct msm_fb_data_type *mfd,
		struct mdp_blit_req *req,
		struct mdp3_img_data *src_data,
		struct mdp3_img_data *dst_data)
{
	int ret;
	unsigned int remainder = 0, is_bpp_4 = 0;

	if (unlikely(req->src_rect.h == 0 || req->src_rect.w == 0)) {
		pr_err("mdp_ppp: src img of zero size!\n");
		return -EINVAL;
	}
	if (unlikely(req->dst_rect.h == 0 || req->dst_rect.w == 0))
		return 0;

	/* MDP width split workaround */
	remainder = (req->dst_rect.w) % 16;
	ret = ppp_get_bpp(req->dst.format, mfd->fb_imgType);
	if (ret <= 0) {
		pr_err("mdp_ppp: incorrect bpp!\n");
		return -EINVAL;
	}
	is_bpp_4 = (ret == 4) ? 1 : 0;

	if ((is_bpp_4 && (remainder == 6 || remainder == 14)) &&
						!(req->flags & MDP_SOLID_FILL))
		ret = mdp3_ppp_blit_workaround(mfd, req, remainder,
							src_data, dst_data);
	else
		ret = mdp3_ppp_blit(mfd, req, src_data, dst_data);
	return ret;
}

void mdp3_ppp_wait_for_fence(struct blit_req_list *req)
{
	int i, ret = 0;
	ATRACE_BEGIN(__func__);
	/* buf sync */
	for (i = 0; i < req->acq_fen_cnt; i++) {
		ret = sync_fence_wait(req->acq_fen[i],
				WAIT_FENCE_FINAL_TIMEOUT);
		if (ret < 0) {
			pr_err("%s: sync_fence_wait failed! ret = %x\n",
				__func__, ret);
			break;
		}
		sync_fence_put(req->acq_fen[i]);
	}
	ATRACE_END(__func__);
	if (ret < 0) {
		while (i < req->acq_fen_cnt) {
			sync_fence_put(req->acq_fen[i]);
			i++;
		}
	}
	req->acq_fen_cnt = 0;
}

void mdp3_ppp_signal_timeline(struct blit_req_list *req)
{
	sw_sync_timeline_inc(ppp_stat->timeline, 1);
	req->last_rel_fence = req->cur_rel_fence;
	req->cur_rel_fence = 0;
}


static void mdp3_ppp_deinit_buf_sync(struct blit_req_list *req)
{
	int i;

	put_unused_fd(req->cur_rel_fen_fd);
	sync_fence_put(req->cur_rel_fence);
	req->cur_rel_fence = NULL;
	req->cur_rel_fen_fd = 0;
	ppp_stat->timeline_value--;
	for (i = 0; i < req->acq_fen_cnt; i++)
		sync_fence_put(req->acq_fen[i]);
	req->acq_fen_cnt = 0;
}

static int mdp3_ppp_handle_buf_sync(struct blit_req_list *req,
	struct mdp_buf_sync *buf_sync)
{
	int i, fence_cnt = 0, ret = 0;
	int acq_fen_fd[MDP_MAX_FENCE_FD];
	struct sync_fence *fence;

	if ((buf_sync->acq_fen_fd_cnt > MDP_MAX_FENCE_FD) ||
		(ppp_stat->timeline == NULL))
		return -EINVAL;

	if (buf_sync->acq_fen_fd_cnt)
		ret = copy_from_user(acq_fen_fd, buf_sync->acq_fen_fd,
				buf_sync->acq_fen_fd_cnt * sizeof(int));
	if (ret) {
		pr_err("%s: copy_from_user failed\n", __func__);
		return ret;
	}
	for (i = 0; i < buf_sync->acq_fen_fd_cnt; i++) {
		fence = sync_fence_fdget(acq_fen_fd[i]);
		if (fence == NULL) {
			pr_info("%s: null fence! i=%d fd=%d\n", __func__, i,
				acq_fen_fd[i]);
			ret = -EINVAL;
			break;
		}
		req->acq_fen[i] = fence;
	}
	fence_cnt = i;
	if (ret)
		goto buf_sync_err_1;
	req->acq_fen_cnt = fence_cnt;
	if (buf_sync->flags & MDP_BUF_SYNC_FLAG_WAIT)
		mdp3_ppp_wait_for_fence(req);

	req->cur_rel_sync_pt = sw_sync_pt_create(ppp_stat->timeline,
			ppp_stat->timeline_value++);
	if (req->cur_rel_sync_pt == NULL) {
		pr_err("%s: cannot create sync point\n", __func__);
		ret = -ENOMEM;
		goto buf_sync_err_2;
	}
	/* create fence */
	req->cur_rel_fence = sync_fence_create("ppp-fence",
			req->cur_rel_sync_pt);
	if (req->cur_rel_fence == NULL) {
		sync_pt_free(req->cur_rel_sync_pt);
		req->cur_rel_sync_pt = NULL;
		pr_err("%s: cannot create fence\n", __func__);
		ret = -ENOMEM;
		goto buf_sync_err_2;
	}
	/* create fd */
	return ret;
buf_sync_err_2:
	ppp_stat->timeline_value--;
buf_sync_err_1:
	for (i = 0; i < fence_cnt; i++)
		sync_fence_put(req->acq_fen[i]);
	req->acq_fen_cnt = 0;
	return ret;
}

void mdp3_ppp_req_push(struct blit_req_queue *req_q, struct blit_req_list *req)
{
	int idx = req_q->push_idx;
	req_q->req[idx] = *req;
	req_q->count++;
	req_q->push_idx = (req_q->push_idx + 1) % MDP3_PPP_MAX_LIST_REQ;
}

struct blit_req_list *mdp3_ppp_next_req(struct blit_req_queue *req_q)
{
	struct blit_req_list *req;
	if (req_q->count == 0)
		return NULL;
	req = &req_q->req[req_q->pop_idx];
	return req;
}

void mdp3_ppp_req_pop(struct blit_req_queue *req_q)
{
	req_q->count--;
	req_q->pop_idx = (req_q->pop_idx + 1) % MDP3_PPP_MAX_LIST_REQ;
}

void mdp3_free_fw_timer_func(unsigned long arg)
{
	schedule_work(&ppp_stat->free_bw_work);
}

static void mdp3_free_bw_wq_handler(struct work_struct *work)
{
	struct msm_fb_data_type *mfd = ppp_stat->mfd;

	mutex_lock(&ppp_stat->config_ppp_mutex);
	if (ppp_stat->bw_on) {
		mdp3_ppp_turnon(mfd, 0);
	}
	mutex_unlock(&ppp_stat->config_ppp_mutex);
}

static bool is_blit_optimization_possible(struct blit_req_list *req, int indx)
{
	int next = indx + 1;
	bool status = false;

	if (!(mdp3_res->smart_blit_en)) {
		pr_debug("Smart BLIT disabled from sysfs\n");
		return status;
	}
	if (next < req->count) {
		/*
		 * Check userspace Smart BLIT Flag for current and next request
		 * Flag for smart blit FG layer index If blit request at index "n" has
		 * MDP_SMART_BLIT flag set then it will be used as BG layer in smart blit
		 * and request at index "n+1" will be used as FG layer
		 */
		if ((req->req_list[indx].flags & MDP_SMART_BLIT) &&
		(!(req->req_list[next].flags & MDP_SMART_BLIT)))
			status = true;
		/*
		 * Enable SMART blit between request 0(BG) & request 1(FG) when
		 * destination ROI of BG and FG layer are same,
		 * No scaling on BG layer
		 * No rotation on BG Layer.
		 * BG Layer color format is RGB
		 */
		else if ((indx == 0) && (!(req->req_list[indx].flags &
			(MDP_ROT_90 | MDP_FLIP_UD | MDP_FLIP_LR))) &&
			(check_if_rgb(req->req_list[indx].src.format)) &&
			(req->req_list[indx].dst_rect.x == req->req_list[next].dst_rect.x) &&
			(req->req_list[indx].dst_rect.y == req->req_list[next].dst_rect.y) &&
			(req->req_list[indx].dst_rect.w == req->req_list[next].dst_rect.w) &&
			(req->req_list[indx].dst_rect.h == req->req_list[next].dst_rect.h) &&
			(req->req_list[indx].dst_rect.w == req->req_list[indx].src_rect.w) &&
			(req->req_list[indx].dst_rect.h == req->req_list[indx].src_rect.h)) {
				status = true;
				req->req_list[indx].flags |= MDP_SMART_BLIT;
			}
		}
	if (status)
		pr_debug("Optimize Blit for Layer: %d Req Count %d\n", indx, req->count) ;
	return status;
}

static void mdp3_ppp_blit_handler(struct kthread_work *work)
{
	struct msm_fb_data_type *mfd = ppp_stat->mfd;
	struct blit_req_list *req;
	int i, rc = 0;
	bool smart_blit = false;
	int smart_blit_fg_index = -1;

	mutex_lock(&ppp_stat->config_ppp_mutex);
	req = mdp3_ppp_next_req(&ppp_stat->req_q);
	if (!req) {
		mutex_unlock(&ppp_stat->config_ppp_mutex);
		return;
	}

	if (!ppp_stat->bw_on) {
		mdp3_ppp_turnon(mfd, 1);
		if (rc < 0) {
			mutex_unlock(&ppp_stat->config_ppp_mutex);
			pr_err("%s: Enable ppp resources failed\n", __func__);
			return;
		}
	}
	while (req) {
		mdp3_ppp_wait_for_fence(req);
		mdp3_calc_ppp_res(mfd, req);
		if (ppp_res.clk_rate != ppp_stat->mdp_clk) {
			ppp_stat->mdp_clk = ppp_res.clk_rate;
			mdp3_clk_set_rate(MDP3_CLK_MDP_SRC,
					ppp_stat->mdp_clk, MDP3_CLIENT_PPP);
		}
		if (ppp_stat->bw_update) {
			rc = mdp3_bus_scale_set_quota(MDP3_CLIENT_PPP,
					ppp_res.next_ab, ppp_res.next_ib);
			if (rc < 0) {
				pr_err("%s: bw set quota failed\n", __func__);
				return;
			}
			ppp_stat->bw_update = false;
		}
		ATRACE_BEGIN("mpd3_ppp_start");
		for (i = 0; i < req->count; i++) {
			smart_blit = is_blit_optimization_possible(req, i);
			if (smart_blit)
				/* Blit request index of FG layer in smart blit */
				smart_blit_fg_index = i + 1;
			if (!(req->req_list[i].flags & MDP_NO_BLIT)) {
				/* Do the actual blit. */
				if (!rc) {
					rc = mdp3_ppp_start_blit(mfd,
						&(req->req_list[i]),
						&req->src_data[i],
						&req->dst_data[i]);
				}
				/* Unmap blit source buffer */
				if (smart_blit == false)
					mdp3_put_img(&req->src_data[i], MDP3_CLIENT_PPP);
				if (smart_blit_fg_index == i) {
					/* Unmap smart blit background buffer */
					mdp3_put_img(&req->src_data[i - 1], MDP3_CLIENT_PPP);
					smart_blit_fg_index = -1;
				}
				mdp3_put_img(&req->dst_data[i], MDP3_CLIENT_PPP);
				smart_blit = false;

			}
		}
		ATRACE_END("mdp3_ppp_start");
		/* Signal to release fence */
		mutex_lock(&ppp_stat->req_mutex);
		mdp3_ppp_signal_timeline(req);
		mdp3_ppp_req_pop(&ppp_stat->req_q);
		req = mdp3_ppp_next_req(&ppp_stat->req_q);
		if (ppp_stat->wait_for_pop)
			complete(&ppp_stat->pop_q_comp);
		mutex_unlock(&ppp_stat->req_mutex);
	}
	mod_timer(&ppp_stat->free_bw_timer, jiffies +
		msecs_to_jiffies(MDP_RELEASE_BW_TIMEOUT));
	mutex_unlock(&ppp_stat->config_ppp_mutex);
}

int mdp3_ppp_parse_req(void __user *p,
	struct mdp_async_blit_req_list *req_list_header,
	int async)
{
	struct blit_req_list *req;
	struct blit_req_queue *req_q = &ppp_stat->req_q;
	struct sync_fence *fence = NULL;
	int count, rc, idx, i;
	count = req_list_header->count;

	mutex_lock(&ppp_stat->req_mutex);
	while (req_q->count >= MDP3_PPP_MAX_LIST_REQ) {
		ppp_stat->wait_for_pop = true;
		mutex_unlock(&ppp_stat->req_mutex);
		rc = wait_for_completion_timeout(
		   &ppp_stat->pop_q_comp, 5 * HZ);
		if (rc == 0) {
			/* This will only occur if there is serious problem */
			pr_err("%s: timeout exiting queuing request\n",
				   __func__);
			return -EBUSY;
		}
		mutex_lock(&ppp_stat->req_mutex);
		ppp_stat->wait_for_pop = false;
	}
	idx = req_q->push_idx;
	req = &req_q->req[idx];

	if (copy_from_user(&req->req_list, p,
			sizeof(struct mdp_blit_req) * count)) {
		mutex_unlock(&ppp_stat->req_mutex);
		return -EFAULT;
	}

	rc = mdp3_ppp_handle_buf_sync(req, &req_list_header->sync);
	if (rc < 0) {
		pr_err("%s: Failed create sync point\n", __func__);
		mutex_unlock(&ppp_stat->req_mutex);
		return rc;
	}
	req->count = count;

	/* We need to grab ion handle while running in client thread */
	for (i = 0; i < count; i++) {
		rc = mdp3_ppp_get_img(&req->req_list[i].src,
				&req->req_list[i], &req->src_data[i]);
		if (rc < 0 || req->src_data[i].len == 0) {
			pr_err("mdp_ppp: couldn't retrieve src img from mem\n");
			goto parse_err_1;
		}

		rc = mdp3_ppp_get_img(&req->req_list[i].dst,
				&req->req_list[i], &req->dst_data[i]);
		if (rc < 0 || req->dst_data[i].len == 0) {
			mdp3_put_img(&req->src_data[i], MDP3_CLIENT_PPP);
			pr_err("mdp_ppp: couldn't retrieve dest img from mem\n");
			goto parse_err_1;
		}
	}

	if (async) {
		req->cur_rel_fen_fd = get_unused_fd_flags(0);
		if (req->cur_rel_fen_fd < 0) {
			pr_err("%s: get_unused_fd_flags failed\n", __func__);
			rc  = -ENOMEM;
			goto parse_err_1;
		}
		sync_fence_install(req->cur_rel_fence, req->cur_rel_fen_fd);
		rc = copy_to_user(req_list_header->sync.rel_fen_fd,
			&req->cur_rel_fen_fd, sizeof(int));
		if (rc) {
			pr_err("%s:copy_to_user failed\n", __func__);
			goto parse_err_2;
		}
	} else {
		fence = req->cur_rel_fence;
	}

	mdp3_ppp_req_push(req_q, req);
	mutex_unlock(&ppp_stat->req_mutex);
	queue_kthread_work(&ppp_stat->kworker, &ppp_stat->blit_work);
	if (!async) {
		/* wait for release fence */
		rc = sync_fence_wait(fence,
				5 * MSEC_PER_SEC);
		if (rc < 0)
			pr_err("%s: sync blit! rc = %x\n", __func__, rc);

		sync_fence_put(fence);
		fence = NULL;
	}
	return 0;

parse_err_2:
	put_unused_fd(req->cur_rel_fen_fd);
parse_err_1:
	for (i--; i >= 0; i--) {
		mdp3_put_img(&req->src_data[i], MDP3_CLIENT_PPP);
		mdp3_put_img(&req->dst_data[i], MDP3_CLIENT_PPP);
	}
	mdp3_ppp_deinit_buf_sync(req);
	mutex_unlock(&ppp_stat->req_mutex);
	return rc;
}

int mdp3_ppp_res_init(struct msm_fb_data_type *mfd)
{
	int rc;
	struct sched_param param = {.sched_priority = 16};
	const char timeline_name[] = "mdp3_ppp";

	ppp_stat = kzalloc(sizeof(struct ppp_status), GFP_KERNEL);
	if (!ppp_stat) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	/*Setup sync_pt timeline for ppp*/
	ppp_stat->timeline = sw_sync_timeline_create(timeline_name);
	if (ppp_stat->timeline == NULL) {
		pr_err("%s: cannot create time line\n", __func__);
		return -ENOMEM;
	} else {
		ppp_stat->timeline_value = 1;
	}

	init_kthread_worker(&ppp_stat->kworker);
	init_kthread_work(&ppp_stat->blit_work, mdp3_ppp_blit_handler);
	ppp_stat->blit_thread = kthread_run(kthread_worker_fn,
					&ppp_stat->kworker,
					"mdp3_ppp");

	if (IS_ERR(ppp_stat->blit_thread)) {
		rc = PTR_ERR(ppp_stat->blit_thread);
		pr_err("ERROR: unable to start ppp blit thread,err = %d\n",
							rc);
		ppp_stat->blit_thread = NULL;
		return rc;
	}
	if (sched_setscheduler(ppp_stat->blit_thread, SCHED_FIFO, &param))
		pr_warn("set priority failed for mdp3 blit thread\n");

	INIT_WORK(&ppp_stat->free_bw_work, mdp3_free_bw_wq_handler);
	init_completion(&ppp_stat->pop_q_comp);
	mutex_init(&ppp_stat->req_mutex);
	mutex_init(&ppp_stat->config_ppp_mutex);
	init_timer(&ppp_stat->free_bw_timer);
	ppp_stat->free_bw_timer.function = mdp3_free_fw_timer_func;
	ppp_stat->free_bw_timer.data = 0;
	ppp_stat->mfd = mfd;
	mdp3_ppp_callback_setup();
	return 0;
}
