# SPDX-License-Identifier: GPL-2.0
dtbo-y += samsung/a12s/a12s_eur_open_w00_r00.dtbo
dtbo-y += samsung/a12s/a12s_eur_open_w00_r01.dtbo
dtbo-y += samsung/m12/m12_eur_open_w00_r00.dtbo
dtbo-y += samsung/m12/m12_eur_open_w00_r01.dtbo
dtb-y += exynos/exynos3830.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
