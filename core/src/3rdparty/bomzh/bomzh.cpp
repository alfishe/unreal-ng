/*
 * =====================================================================================================
 * bomzh.c — C-модель видеоадаптера «бомж-3D»: что должна нарисовать ПЛИС, точка в точку.
 * =====================================================================================================
 *
 * ЗАЧЕМ МОДЕЛЬ, ЕСЛИ ЕСТЬ VERILOG
 *   Схему в ПЛИС отлаживать долго: сборка — минуты, на плате видно только картинку. Поэтому сначала
 *   то же самое пишется на C: программу можно прогнать на ПК на тысячах кадров настоящего Quake 2 за
 *   секунды, сравнить с программным рендером id и найти ошибки в самой идее. Потом Verilog (gpu/rtl)
 *   сверяется с этой моделью на тех же кадрах — побайтно, по всей памяти.
 *
 * ГЛАВНОЕ ПРАВИЛО ФАЙЛА
 *   Никакой плавающей точки (float, double) и никакого «примерно». Если здесь написано «(a·b) >> 16», то
 *   и RTL делает ровно это, с той же шириной чисел. Тогда кадр RTL можно сравнить с кадром модели до
 *   бита. С программным Quake 2 модель сравнивается уже «на глаз» (results/bz): там разница допустима —
 *   другой растеризатор и свет в каждой точке, а не в каждом текселе.
 *   Дробные числа — с фиксированной точкой (Q-формат): Qm.n хранит x как целое x · 2^n (см. SPEC.md).
 *
 * КАК УСТРОЕНО (сверху вниз по файлу)
 *   память SDRAM     — массив байт mem; rd8/wr8/rd16/wr16 — как обращения ПЛИС к SDRAM;
 *   арифметика       — grad (значение градиента C + A·x + B·y), bz_zenc («плавающий» z), persp
 *                      (деление для перспективы), asr (сдвиг со знаком);
 *   кэш текстур      — только считает попадания и промахи (сама память — mem), чтобы оценить SDRAM;
 *   poly             — многоугольник: верхняя вершина, две цепочки рёбер, отрезок каждой строки;
 *   segment          — кусок отрезка строки (до 16 точек, выровнен по колонкам экрана) и его точки:
 *                      z-тест, тексель, свет, colormap, прозрачность, запись;
 *   point/pic/fill   — частица, картинка 2D, заливка;
 *   bz_exec          — разбор потока команд (SPEC.md, «Команды»).
 * Попутно копятся счётчики bz_stats_t: по ним видно, сколько тактов и пачек SDRAM нужно сцене
 * (tools/bz_summary.py).
 */
#include "bomzh.h"

#include <stdlib.h>
#include <string.h>

struct bz_s
{
	uint8_t   *mem;              /* копия SDRAM                                               */
	uint32_t   mask;             /* размер памяти − 1 (размер — степень двойки)               */
	int        W, H;             /* экран                                                     */
	int        S;                /* шаг строки кадра и z в точках: W вверх до 16 (SPEC.md)    */
	uint32_t   fb[2], zaddr;     /* два кадра (8 бит) и z (16 бит) в SDRAM                    */
	uint32_t   amap;             /* таблица прозрачности 256×256 в SDRAM                      */
	int        draw;             /* номер рисуемого кадра; показан — другой                   */
	uint8_t    colormap[64 * 256];   /* в BSRAM: 64 строки света × 256 цветов                 */
	uint8_t    pal[768];         /* палитра вывода (BSRAM)                                    */
	int32_t    turb[128];        /* синус волны (BSRAM)                                       */
	int        phase;            /* фаза волны 0…127 (команда TIME)                          */
	uint32_t   mark;             /* регистр MARK: число последней выполненной команды MARK    */
	uint8_t    blend[4];         /* вспышка R, G, B, доля A (команда FRAME)                   */

	/* текущая текстура (TEX) */
	uint32_t   t_addr;
	int        t_lin;            /* 1 — LINEAR (строки подряд, края зажаты)                   */
	int        t_wl, t_hl;       /* TILED: log2 ширины и высоты                               */
	int        t_lw, t_lh, t_ls; /* LINEAR: ширина, высота, шаг строки                        */

	/* текущий lightmap (LMAP) */
	uint32_t   l_addr;
	int        l_stride, l_rows, l_shift;
	int32_t    l_smin, l_tmin;

	int        sx0, sy0, sx1, sy1;   /* ножницы: [sx0, sx1) × [sy0, sy1)                      */

	/* кэш текстур: 64 набора × 2 пути × строка 64 байта = 8 КБ (GPU.md) */
	uint32_t   tc_tag[64][2];
	uint8_t    tc_ok[64][2];
	uint8_t    tc_old[64];       /* какой путь вытеснять следующим (LRU на два пути)          */

	/* буферы моделей (SPEC.md, «Треугольники моделей»): в ПЛИС — BSRAM на 512 записей */
	int16_t    vb_u[512], vb_v[512];    /* вершина: целая точка экрана                       */
	int32_t    vb_zi[512], vb_l[512];   /* 1/z (Q2.30) и свет (0…16383)                      */
	int16_t    st_s[512], st_t[512];    /* точка шкурки: целые тексели                       */

	bz_stats_t st;

	bz_poly_hook_t  hook_poly;         /* крючки стендов (bz_set_hooks)                      */
	bz_pixel_hook_t hook_pixel;
	void           *hook_user;
};

/* ---------------------------------------------------------------- память SDRAM */

static BZ_INLINE uint8_t rd8 (const bz_t *bz, uint32_t a) { return bz->mem[a & bz->mask]; }
static BZ_INLINE void wr8 (bz_t *bz, uint32_t a, uint8_t v) { bz->mem[a & bz->mask] = v; }
static BZ_INLINE uint16_t rd16 (const bz_t *bz, uint32_t a)
{
	return (uint16_t)(rd8 (bz, a) | (rd8 (bz, a + 1) << 8));
}
static BZ_INLINE void wr16 (bz_t *bz, uint32_t a, uint16_t v)
{
	wr8 (bz, a, (uint8_t)v);
	wr8 (bz, a + 1, (uint8_t)(v >> 8));
}
static BZ_INLINE uint32_t rd32 (const bz_t *bz, uint32_t a)
{
	return rd8 (bz, a) | (rd8 (bz, a + 1) << 8) | (rd8 (bz, a + 2) << 16) | ((uint32_t)rd8 (bz, a + 3) << 24);
}

/* ---------------------------------------------------------------- целая арифметика как в RTL */

/* Арифметический сдвиг вправо (знак размножается) — так делает RTL; в C для отрицательных
 * чисел это «определяется реализацией», поэтому — явно. */
static BZ_INLINE int64_t asr64 (int64_t v, int n) { return v >= 0 ? v >> n : ~((~v) >> n); }
static BZ_INLINE int32_t asr32 (int32_t v, int n) { return v >= 0 ? v >> n : ~((~v) >> n); }

/* Деление с округлением вверх при положительном делителе. */
static BZ_INLINE int64_t ceil_div (int64_t a, int64_t b)
{
	int64_t q = a / b, r = a % b;
	return (r > 0) ? q + 1 : q;      /* C делит к нулю: для a < 0 это уже округление вверх */
}

/* Значение градиента {C, A, B} в точке (x, y): C + A·x + B·y по модулю 2^32. В RTL это не
 * умножение на каждую точку, а прибавка A на шаг вправо — результат тот же до бита. */
static BZ_INLINE int32_t grad (const int32_t g[3], int x, int y)
{
	return (int32_t)((uint32_t)g[0] + (uint32_t)g[1] * (uint32_t)x + (uint32_t)g[2] * (uint32_t)y);
}

/* z в 16 бит: «плавающее» число из zi (Q2.30), 5 бит порядка + 11 бит мантиссы.
 * Почему не просто zi >> k: 1/z в линейном виде теряет точность вдали (на 2000 единиц — шаг
 * 16 единиц), а плавающее держит одну и ту же относительную точность 1/2048 на любой глубине.
 * Порядок монотонный: больше zi (ближе) — больше z16. Схема: счётчик ведущих нулей и сдвиг. */
uint16_t bz_zenc (int32_t zi)
{
	uint32_t v;
	int      e;

	if (zi <= 0)
		return 0;
	v = (uint32_t)zi;
	for (e = 30; !(v >> e); e--)
		;
	if (e < 11)
		return (uint16_t)v;                     /* «денормалы»: значение как есть        */
	return (uint16_t)(((e - 10) << 11) | ((v >> (e - 11)) & 0x7FF));
}

/* Перспектива: s = (su · floor(2^46 / zi)) >> 20. su — Q12.20 (s·zi), zi — Q2.30, s — Q16.16.
 * zi зажат снизу 2^15 (глубина ≤ 32768 единиц), чтобы частное влезло в 32 бита. В RTL —
 * последовательный делитель по 2 бита за такт: 16 тактов на конец куска из 16 точек. */
static BZ_INLINE int32_t persp (int32_t su, int32_t zi)
{
	int64_t  z = zi < (1 << 15) ? (1 << 15) : zi;
	uint64_t w = ((uint64_t)1 << 46) / (uint64_t)z;          /* ≤ 2^31 */
	return (int32_t)(uint32_t)asr64 ((int64_t)su * (int64_t)w, 20);
}

/* ---------------------------------------------------------------- кэш текстур (только счёт) */

static BZ_INLINE void tex_touch (bz_t *bz, uint32_t a)
{
	uint32_t line = (a & bz->mask) >> 6;
	int      set = line & 63, way;
	uint32_t tag = line >> 6;

	bz->st.tex_reads++;
	for (way = 0; way < 2; way++)
		if (bz->tc_ok[set][way] && bz->tc_tag[set][way] == tag)
		{
			bz->tc_old[set] = (uint8_t)(way ^ 1);
			return;
		}
	way = bz->tc_old[set];
	bz->tc_tag[set][way] = tag;
	bz->tc_ok[set][way] = 1;
	bz->tc_old[set] = (uint8_t)(way ^ 1);
	bz->st.tex_miss++;
}

/* Тексель по s, t (Q16.16). TILED — повтор по маске и плитки 8×8 по 64 байта; LINEAR — края
 * зажаты, строки подряд с шагом t_ls. */
static BZ_INLINE uint8_t texel (bz_t *bz, int32_t s, int32_t t)
{
	uint32_t a;

	if (bz->t_lin)
	{
		int x = asr32 (s, 16), y = asr32 (t, 16);
		if (x < 0) x = 0;
		if (x > bz->t_lw - 1) x = bz->t_lw - 1;
		if (y < 0) y = 0;
		if (y > bz->t_lh - 1) y = bz->t_lh - 1;
		a = bz->t_addr + (uint32_t)y * (uint32_t)bz->t_ls + (uint32_t)x;
	}
	else
	{
		uint32_t x = (uint32_t)asr32 (s, 16) & ((1u << bz->t_wl) - 1);
		uint32_t y = (uint32_t)asr32 (t, 16) & ((1u << bz->t_hl) - 1);
		if (bz->t_wl >= 3 && bz->t_hl >= 3)
			a = bz->t_addr + ((((y >> 3) << (bz->t_wl - 3)) + (x >> 3)) << 6) + ((y & 7) << 3) + (x & 7);
		else
			a = bz->t_addr + (y << bz->t_wl) + x;
	}
	tex_touch (bz, a);
	return rd8 (bz, a);
}

/* ---------------------------------------------------------------- многоугольник */

typedef struct
{
	int       mode;
	int32_t   g[4][3];         /* zi, u (su или s), v (tv или t), l */
	int       lm_iu, lm_iv;    /* клетка lightmap прошлой точки (для счёта выборок) */
} poly_t;

/* Строка света 0…63 из lightmap: билинейно по четырём клеткам, веса — 4 бита дроби. */
static BZ_INLINE int lm_row (bz_t *bz, poly_t *p, int32_t s, int32_t t)
{
	int32_t u = asr32 ((int32_t)((uint32_t)s - (uint32_t)bz->l_smin), bz->l_shift);
	int32_t v = asr32 ((int32_t)((uint32_t)t - (uint32_t)bz->l_tmin), bz->l_shift);
	int     iu = asr32 (u, 16), iv = asr32 (v, 16), fu = (u >> 12) & 15, fv = (v >> 12) & 15;
	int     iu1, iv1;
	uint32_t r0, r1, val;

	if (iu < 0) { iu = 0; fu = 0; }
	if (iv < 0) { iv = 0; fv = 0; }
	if (iu > bz->l_stride - 1) iu = bz->l_stride - 1;
	if (iv > bz->l_rows - 1) iv = bz->l_rows - 1;
	iu1 = iu + 1 > bz->l_stride - 1 ? bz->l_stride - 1 : iu + 1;
	iv1 = iv + 1 > bz->l_rows - 1 ? bz->l_rows - 1 : iv + 1;
	if (iu != p->lm_iu || iv != p->lm_iv)
	{
		bz->st.lm_fetch++;
		p->lm_iu = iu;
		p->lm_iv = iv;
	}
	r0 = bz->l_addr + (uint32_t)iv * (uint32_t)bz->l_stride;
	r1 = bz->l_addr + (uint32_t)iv1 * (uint32_t)bz->l_stride;
	val = (rd8 (bz, r0 + iu) * (16 - fu) + rd8 (bz, r0 + iu1) * fu) * (16 - fv) +
	      (rd8 (bz, r1 + iu) * (16 - fu) + rd8 (bz, r1 + iu1) * fu) * fv;
	return (int)(val >> 10);
}

/* Смешивание прозрачности через alphamap Quake (64 КБ в SDRAM). */
static BZ_INLINE uint8_t trans_mix (const bz_t *bz, int trans, uint8_t src, uint8_t dst)
{
	if (trans == BZ_T_33)
		return rd8 (bz, bz->amap + src + ((uint32_t)dst << 8));
	return rd8 (bz, bz->amap + ((uint32_t)src << 8) + dst);
}

/* Кусок строки: точки x0…x0+n−1 строки y, n ≤ 16, все в одной колонке по 16 точек. */
static void segment (bz_t *bz, poly_t *p, int y, int x0, int n, int first)
{
	static const int32_t R[17] = { 0, 65536, 32768, 21845, 16384, 13107, 10922, 9362, 8192,
	                               7281, 6553, 5957, 5461, 5041, 4681, 4369, 4096 };
	const int mode = p->mode;
	const int light = mode & BZ_M_LIGHT, trans = mode & BZ_M_TRANS;
	const int sky = (mode & BZ_M_SKY) != 0;
	const int ztest = sky || (mode & BZ_M_ZTEST);
	const int zgt = !sky && (mode & BZ_M_ZGT);
	const int zwrite = !sky && (mode & BZ_M_ZWRITE);
	int32_t   s0 = 0, t0 = 0, ds = 0, dt = 0;
	int       k, zr = 0, zw = 0, cr = 0, cw = 0;
	uint32_t  row = (uint32_t)y * (uint32_t)bz->S;

	bz->st.seg++;
	if (mode & BZ_M_PERSP)
	{
		int32_t s1, t1;
		s0 = persp (grad (p->g[1], x0, y), grad (p->g[0], x0, y));
		t0 = persp (grad (p->g[2], x0, y), grad (p->g[0], x0, y));
		s1 = persp (grad (p->g[1], x0 + n, y), grad (p->g[0], x0 + n, y));
		t1 = persp (grad (p->g[2], x0 + n, y), grad (p->g[0], x0 + n, y));
		/* конец куска = начало следующего: в RTL одно деление на кусок, у первого — два */
		bz->st.divides += first ? 2 : 1;
		ds = (int32_t)asr64 (((int64_t)s1 - s0) * R[n], 16);
		dt = (int32_t)asr64 (((int64_t)t1 - t0) * R[n], 16);
	}

	for (k = 0; k < n; k++)
	{
		const int x = x0 + k;
		const int32_t zi = grad (p->g[0], x, y);
		if (bz->hook_pixel)
			bz->hook_pixel (bz->hook_user, x, y, zi,
			                (mode & BZ_M_PERSP) ? (int32_t)((uint32_t)s0 + (uint32_t)k * (uint32_t)ds) : grad (p->g[1], x, y),
			                (mode & BZ_M_PERSP) ? (int32_t)((uint32_t)t0 + (uint32_t)k * (uint32_t)dt) : grad (p->g[2], x, y),
			                grad (p->g[3], x, y));
		const uint16_t z16 = sky ? 0 : bz_zenc (zi);
		const uint32_t za = bz->zaddr + 2 * (row + (uint32_t)x);
		int32_t s, t, ws, wt;
		uint8_t tx, c;

		bz->st.px_raster++;
		if (ztest)
		{
			uint16_t zb = rd16 (bz, za);
			zr = 1;
			if (zgt ? !(z16 > zb) : !(z16 >= zb))
			{
				bz->st.px_zfail++;
				continue;
			}
		}
		if (mode & BZ_M_PERSP)
		{
			s = (int32_t)((uint32_t)s0 + (uint32_t)k * (uint32_t)ds);
			t = (int32_t)((uint32_t)t0 + (uint32_t)k * (uint32_t)dt);
		}
		else
		{
			s = grad (p->g[1], x, y);
			t = grad (p->g[2], x, y);
		}
		ws = s;
		wt = t;
		if (mode & BZ_M_WARP)
		{
			ws = (int32_t)((uint32_t)s + (uint32_t)bz->turb[(asr32 (t, 16) + bz->phase) & 127]);
			wt = (int32_t)((uint32_t)t + (uint32_t)bz->turb[(asr32 (s, 16) + bz->phase) & 127]);
		}
		tx = texel (bz, ws, wt);
		if ((mode & BZ_M_KEY) && tx == 255)
		{
			bz->st.px_key++;
			continue;
		}
		if (light == BZ_L_LMAP)
			c = bz->colormap[(lm_row (bz, p, s, t) << 8) + tx];
		else if (light == BZ_L_VERT)
		{
			int r = asr32 (grad (p->g[3], x, y), 24);
			if (r < 0) r = 0;
			if (r > 63) r = 63;
			c = bz->colormap[(r << 8) + tx];
		}
		else
			c = tx;
		if (trans)
		{
			c = trans_mix (bz, trans, c, rd8 (bz, bz->fb[bz->draw] + row + (uint32_t)x));
			cr = 1;
		}
		wr8 (bz, bz->fb[bz->draw] + row + (uint32_t)x, c);
		cw = 1;
		bz->st.px_written++;
		if (zwrite)
		{
			wr16 (bz, za, z16);
			zw = 1;
		}
	}
	bz->st.seg_zread += zr;
	bz->st.seg_zwrite += zw;
	bz->st.seg_cread += cr;
	bz->st.seg_cwrite += cw;
}

/* Край ребра p→q (p.y ≤ yc < q.y) в строке с центром yc: первая точка, чей центр не левее ребра. */
static BZ_INLINE int64_t edge_x (int px, int py, int qx, int qy, int yc)
{
	int64_t dy = qy - py, dx = qx - px;
	return ceil_div ((int64_t)(px - 8) * dy + (int64_t)(yc - py) * dx, 16 * dy);
}

/* Многоугольник: вершины xy (Q12.4), n ≤ 16, выпуклый, обход любой. Центр точки (x, y) —
 * (16x + 8, 16y + 8). Край включается слева и сверху, справа и снизу — нет (SPEC.md).
 *
 * Обход — двумя цепочками от верхней вершины (вперёд и назад по номерам) до нижней, как это
 * делает железо: на строку нужны только два ребра, а их край в RTL идёт приращением (DDA), деление —
 * одно на начало ребра. Для выпуклого многоугольника это то же, что «крайние левый и правый
 * края по всем рёбрам»; для невыпуклого (вершины почти на одной прямой после округления до 1/16)
 * результат тоже однозначен — тот, что здесь. */
static void poly (bz_t *bz, const uint32_t *xy, int n, poly_t *p)
{
	int vx[16], vy[16], i, y, y0, y1, top = 0, bot = 0, a0, a1, b0, b1;

	for (i = 0; i < n; i++)
	{
		vx[i] = (int16_t)(xy[i] & 0xFFFF);
		vy[i] = (int16_t)(xy[i] >> 16);
		if (vy[i] < vy[top]) top = i;          /* первая из верхних */
		if (vy[i] > vy[bot]) bot = i;          /* первая из нижних  */
	}
	y0 = (vy[top] + 7) >> 4;       /* >> у отрицательных — вниз (floor) во всех наших компиляторах */
	y1 = (vy[bot] + 7) >> 4;
	if (y0 < bz->sy0) y0 = bz->sy0;
	if (y1 > bz->sy1) y1 = bz->sy1;
	bz->st.polys++;
	if (bz->hook_poly)
		bz->hook_poly (bz->hook_user, xy, n, p->mode, (const int32_t (*)[3])p->g, bz->sx0, bz->sy0, bz->sx1, bz->sy1);
	a0 = b0 = top;
	a1 = top + 1 == n ? 0 : top + 1;           /* цепочка A — вперёд по номерам */
	b1 = top == 0 ? n - 1 : top - 1;           /* цепочка B — назад            */

	for (y = y0; y < y1; y++)
	{
		const int yc = 16 * y + 8;
		int64_t xa, xb, xl, xr;
		int     x;

		/* ребро цепочки, на котором кончается строка: пройти рёбра, закончившиеся выше центра */
		while (a1 != bot && vy[a1] <= yc)
		{
			a0 = a1;
			a1 = a1 + 1 == n ? 0 : a1 + 1;
		}
		while (b1 != bot && vy[b1] <= yc)
		{
			b0 = b1;
			b1 = b1 == 0 ? n - 1 : b1 - 1;
		}
		if (!(vy[a0] <= yc && yc < vy[a1]) || !(vy[b0] <= yc && yc < vy[b1]))
			continue;
		xa = edge_x (vx[a0], vy[a0], vx[a1], vy[a1], yc);
		xb = edge_x (vx[b0], vy[b0], vx[b1], vy[b1], yc);
		xl = xa < xb ? xa : xb;
		xr = xa < xb ? xb : xa;
		if (xl < bz->sx0) xl = bz->sx0;
		if (xr > bz->sx1) xr = bz->sx1;
		p->lm_iu = p->lm_iv = -1;
		for (x = (int)xl; x < (int)xr; )
		{
			int n16 = ((x | 15) + 1 < (int)xr ? (x | 15) + 1 : (int)xr) - x;
			segment (bz, p, y, x, n16, x == (int)xl);
			x += n16;
		}
	}
}

/* ---------------------------------------------------------------- треугольники моделей */

/* Деление с округлением вниз (к минус бесконечности) при d > 0. В C «/» округляет к нулю: для
 * отрицательного делимого с остатком надо вычесть 1 (−7 / 2 = −3, а floor(−3,5) = −4). */
static int64_t floor_div (int64_t n, int64_t d)
{
	int64_t q = n / d;
	return (n % d != 0 && n < 0) ? q - 1 : q;
}

/* Постановка треугольника (SPEC.md, «Треугольники моделей»): по трём вершинам из буферов — плоскость
 * каждой величины {C, A, B}, чтобы дальше рисовать его как обычный POLY.
 *
 * Плоскость через три точки: величина f меняется на экране линейно, f(x, y) = C + A·x + B·y. Подставив
 * три вершины, получаем систему из трёх уравнений; её решение (правило Крамера) — формулы ниже, общий
 * знаменатель d — удвоенная площадь треугольника со знаком. Знак d говорит, какой стороной треугольник
 * повёрнут к нам: d ≤ 0 — спиной (или он вырожден в отрезок), такой не рисуется, как у Quake.
 * Делим точно, с округлением вниз — тогда ПЛИС получит те же A и B до бита. */
static int tri_setup (const bz_t *bz, const int vi[3], const int si[3], uint32_t xy[3], int32_t g[4][3])
{
	int64_t u[3], v[3], f[4][3], du1, dv1, du2, dv2, d;
	int     k, i;

	for (i = 0; i < 3; i++)
	{
		u[i] = bz->vb_u[vi[i]];
		v[i] = bz->vb_v[vi[i]];
		f[0][i] = bz->vb_zi[vi[i]];
		f[1][i] = (int32_t)((uint32_t)(int32_t)bz->st_s[si[i]] << 16);
		f[2][i] = (int32_t)((uint32_t)(int32_t)bz->st_t[si[i]] << 16);
		f[3][i] = (int32_t)((uint32_t)bz->vb_l[vi[i]] << 16);
		xy[i] = bz_xy ((int)(u[i] * 16 + 8), (int)(v[i] * 16 + 8));
	}
	du1 = u[1] - u[0]; dv1 = v[1] - v[0];
	du2 = u[2] - u[0]; dv2 = v[2] - v[0];
	d = du1 * dv2 - du2 * dv1;
	if (d <= 0)
		return 0;
	for (k = 0; k < 4; k++)
	{
		const int64_t df1 = f[k][1] - f[k][0], df2 = f[k][2] - f[k][0];
		const int64_t a = floor_div (df1 * dv2 - df2 * dv1, d);
		const int64_t b = floor_div (du1 * df2 - du2 * df1, d);
		g[k][0] = (int32_t)((uint32_t)f[k][0] - (uint32_t)a * (uint32_t)u[0] - (uint32_t)b * (uint32_t)v[0]);
		g[k][1] = (int32_t)(uint32_t)a;
		g[k][2] = (int32_t)(uint32_t)b;
	}
	return 1;
}

static void tris (bz_t *bz, const uint32_t *w)
{
	const int n = w[0] & 0xFF, mode = (w[0] >> 8) & 0xFFFF;
	int k;

	for (k = 0; k < n; k++)
	{
		const uint32_t a = w[1 + 2 * k], b = w[2 + 2 * k];
		const int vi[3] = { (int)(a & 511), (int)((a >> 9) & 511), (int)((a >> 18) & 511) };
		const int si[3] = { (int)(b & 511), (int)((b >> 9) & 511), (int)((b >> 18) & 511) };
		uint32_t xy[3];
		poly_t   p;

		bz->st.tris++;
		p.mode = mode;
		if (!tri_setup (bz, vi, si, xy, p.g))
		{
			bz->st.tris_culled++;
			continue;
		}
		poly (bz, xy, 3, &p);
	}
}

/* ---------------------------------------------------------------- частица, картинка, заливка */

/* Сколько колонок по 16 точек задевает отрезок [x0, x1) — столько пачек SDRAM на строку. */
static BZ_INLINE int chunks (int x0, int x1) { return x1 > x0 ? ((x1 - 1) >> 4) - (x0 >> 4) + 1 : 0; }

static void point (bz_t *bz, const uint32_t *w)
{
	const uint8_t color = (uint8_t)w[0];
	const int size = (w[0] >> 8) & 15, trans = ((w[0] >> 12) & 3) << 7;
	const int x = (int16_t)(w[1] & 0xFFFF), y = (int16_t)(w[1] >> 16);
	const uint16_t z16 = bz_zenc ((int32_t)w[2]);
	int i, j;

	bz->st.points++;
	for (j = 0; j < size; j++)
	{
		const int py = y + j;
		int any = 0;
		if (py < bz->sy0 || py >= bz->sy1)
			continue;
		for (i = 0; i < size; i++)
		{
			const int px = x + i;
			const uint32_t pa = bz->fb[bz->draw] + (uint32_t)py * (uint32_t)bz->S + (uint32_t)px;
			const uint32_t za = bz->zaddr + 2 * ((uint32_t)py * (uint32_t)bz->S + (uint32_t)px);
			if (px < bz->sx0 || px >= bz->sx1)
				continue;
			bz->st.px_raster++;
			any = 1;
			if (z16 < rd16 (bz, za))
			{
				bz->st.px_zfail++;
				continue;
			}
			/* как R_DrawParticle Quake: z пишется на всех уровнях прозрачности */
			wr8 (bz, pa, trans ? trans_mix (bz, trans, color, rd8 (bz, pa)) : color);
			wr16 (bz, za, z16);
			bz->st.px_written++;
		}
		if (any)
		{
			int c = chunks (x < bz->sx0 ? bz->sx0 : x, x + size > bz->sx1 ? bz->sx1 : x + size);
			bz->st.seg += c;
			bz->st.seg_zread += c;
			bz->st.seg_zwrite += c;
			bz->st.seg_cwrite += c;
			if (trans)
				bz->st.seg_cread += c;
		}
	}
}

static void pic (bz_t *bz, const uint32_t *w)
{
	const int key = w[0] & 1;
	const int x = (int16_t)(w[1] & 0xFFFF), y = (int16_t)(w[1] >> 16);
	const int pw = w[2] & 0xFFFF, ph = w[2] >> 16;
	const uint32_t addr = w[3], stride = w[4], ss = w[5], ts = w[6];
	int i, j;

	bz->st.pics++;
	for (j = 0; j < ph; j++)
	{
		const int py = y + j;
		const uint32_t src = addr + (uint32_t)(((uint64_t)j * ts) >> 16) * stride;
		int x0 = x < 0 ? 0 : x, x1 = x + pw > bz->W ? bz->W : x + pw;
		if (py < 0 || py >= bz->H || x0 >= x1)
			continue;
		for (i = x0 - x; i < x1 - x; i++)
		{
			const uint32_t a = src + (uint32_t)(((uint64_t)i * ss) >> 16);
			uint8_t c;
			tex_touch (bz, a);
			c = rd8 (bz, a);
			if (key && c == 255)
			{
				bz->st.px_key++;
				continue;
			}
			wr8 (bz, bz->fb[bz->draw] + (uint32_t)py * (uint32_t)bz->S + (uint32_t)(x + i), c);
			bz->st.px_written++;
		}
		bz->st.seg_cwrite += chunks (x0, x1);
	}
}

static void fill (bz_t *bz, const uint32_t *w)
{
	const int target = w[0] & 1, fade = (w[0] >> 1) & 1;
	int x0 = (int16_t)(w[1] & 0xFFFF), y0 = (int16_t)(w[1] >> 16);
	int x1 = x0 + (int)(w[2] & 0xFFFF), y1 = y0 + (int)(w[2] >> 16);
	int x, y;

	bz->st.fills++;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > bz->W) x1 = bz->W;
	if (y1 > bz->H) y1 = bz->H;
	for (y = y0; y < y1; y++)
	{
		const uint32_t row = (uint32_t)y * (uint32_t)bz->S;
		for (x = x0; x < x1; x++)
		{
			if (target)
				wr16 (bz, bz->zaddr + 2 * (row + (uint32_t)x), (uint16_t)w[3]);
			else if (!fade)
				wr8 (bz, bz->fb[bz->draw] + row + (uint32_t)x, (uint8_t)w[3]);
			else if ((x & 3) != ((y & 1) << 1))       /* как Draw_FadeScreen Quake */
				wr8 (bz, bz->fb[bz->draw] + row + (uint32_t)x, 0);
			else
				continue;
			bz->st.px_fill++;
		}
		if (target)
			bz->st.seg_zwrite += chunks (x0, x1);
		else
			bz->st.seg_cwrite += chunks (x0, x1);
	}
}

/* ---------------------------------------------------------------- разбор команд */

static void set_screen_scissor (bz_t *bz)
{
	bz->sx0 = bz->sy0 = 0;
	bz->sx1 = bz->W;
	bz->sy1 = bz->H;
}

uint32_t bz_exec (bz_t *bz, const uint32_t *w, uint32_t count)
{
	uint32_t i = 0;

	while (i < count)
	{
		const uint32_t w0 = w[i];
		const int      op = (int)(w0 >> 24);
		uint32_t       len;

		switch (op)
		{
		case BZ_TEX:     len = 3; break;
		case BZ_LMAP:    len = 5; break;
		case BZ_POLY:
		{
			const int n = w0 & 0xFF, mode = (w0 >> 8) & 0xFFFF;
			if (n < 3 || n > 16)
				return i + 1;
			len = 1 + n + 9 + ((mode & BZ_M_LIGHT) == BZ_L_VERT ? 3 : 0);
			break;
		}
		case BZ_POINT:   len = 3; break;
		case BZ_PIC:     len = 7; break;
		case BZ_FILL:    len = 4; break;
		case BZ_FRAME:   len = 2; break;
		case BZ_TIME:    len = 2; break;
		case BZ_WRITE:   len = 2 + (w0 & 0xFFFFFF); break;
		case BZ_SETUP:   len = 9; break;
		case BZ_SCISSOR: len = 3; break;
		case BZ_VBUF:    if ((w0 & 0x3FF) < 1 || (w0 & 0x3FF) > 512) return i + 1; len = 1 + 3 * (w0 & 0x3FF); break;
		case BZ_STBUF:   if ((w0 & 0x3FF) < 1 || (w0 & 0x3FF) > 512) return i + 1; len = 1 + (w0 & 0x3FF); break;
		case BZ_TRIS:    len = 1 + 2 * (w0 & 0xFF); break;
		case BZ_MARK:    len = 2; break;
		default:         return i + 1;
		}
		if (i + len > count)
			return i + 1;
		bz->st.cmd_words += (op == BZ_WRITE) ? 2 : len;

		switch (op)
		{
		case BZ_TEX:
			bz->t_addr = w[i + 1];
			bz->t_lin = (w0 >> 16) & 1;
			if (bz->t_lin)
			{
				bz->t_lw = w0 & 0xFFFF;
				bz->t_lh = w[i + 2] & 0xFFFF;
				bz->t_ls = w[i + 2] >> 16;
				if (bz->t_lw < 1) bz->t_lw = 1;
				if (bz->t_lh < 1) bz->t_lh = 1;
			}
			else
			{
				bz->t_wl = w[i + 2] & 15;
				bz->t_hl = (w[i + 2] >> 4) & 15;
			}
			break;
		case BZ_LMAP:
			bz->l_addr = w[i + 1];
			bz->l_stride = w[i + 2] & 0xFFFF;
			bz->l_rows = (w[i + 2] >> 16) & 0xFF;
			bz->l_shift = (w[i + 2] >> 24) & 15;
			bz->l_smin = (int32_t)w[i + 3];
			bz->l_tmin = (int32_t)w[i + 4];
			if (bz->l_stride < 1) bz->l_stride = 1;
			if (bz->l_rows < 1) bz->l_rows = 1;
			break;
		case BZ_POLY:
		{
			poly_t p;
			const int n = w0 & 0xFF;
			const uint32_t *g = w + i + 1 + n;
			int a, b;
			p.mode = (w0 >> 8) & 0xFFFF;
			memset (p.g, 0, sizeof(p.g));
			for (a = 0; a < (((p.mode & BZ_M_LIGHT) == BZ_L_VERT) ? 4 : 3); a++)
				for (b = 0; b < 3; b++)
					p.g[a][b] = (int32_t)g[a * 3 + b];
			poly (bz, w + i + 1, n, &p);
			break;
		}
		case BZ_POINT:   point (bz, w + i); break;
		case BZ_VBUF:
		{
			uint32_t k;
			for (k = 0; k < (w0 & 0x3FF); k++)
			{
				bz->vb_u[k] = (int16_t)(w[i + 1 + 3 * k] & 0xFFFF);
				bz->vb_v[k] = (int16_t)(w[i + 1 + 3 * k] >> 16);
				bz->vb_zi[k] = (int32_t)w[i + 2 + 3 * k];
				bz->vb_l[k] = (int32_t)w[i + 3 + 3 * k];
			}
			break;
		}
		case BZ_STBUF:
		{
			uint32_t k;
			for (k = 0; k < (w0 & 0x3FF); k++)
			{
				bz->st_s[k] = (int16_t)(w[i + 1 + k] & 0xFFFF);
				bz->st_t[k] = (int16_t)(w[i + 1 + k] >> 16);
			}
			break;
		}
		case BZ_TRIS:    tris (bz, w + i); break;
		case BZ_PIC:     pic (bz, w + i); break;
		case BZ_FILL:    fill (bz, w + i); break;
		case BZ_FRAME:
			bz->blend[0] = (uint8_t)w[i + 1];
			bz->blend[1] = (uint8_t)(w[i + 1] >> 8);
			bz->blend[2] = (uint8_t)(w[i + 1] >> 16);
			bz->blend[3] = (uint8_t)(w[i + 1] >> 24);
			bz->draw ^= 1;
			bz->st.frames++;
			break;
		case BZ_TIME:
			bz->phase = w[i + 1] & 127;
			break;
		case BZ_MARK:
			/* SPEC 0.4: всё, что шло в потоке раньше, уже выполнено (в железе — записано в SDRAM) */
			bz->mark = w[i + 1];
			break;
		case BZ_WRITE:
		{
			uint32_t k, a = w[i + 1];
			for (k = 0; k < (w0 & 0xFFFFFF); k++, a += 4)
			{
				wr8 (bz, a, (uint8_t)w[i + 2 + k]);
				wr8 (bz, a + 1, (uint8_t)(w[i + 2 + k] >> 8));
				wr8 (bz, a + 2, (uint8_t)(w[i + 2 + k] >> 16));
				wr8 (bz, a + 3, (uint8_t)(w[i + 2 + k] >> 24));
			}
			bz->st.write_bytes += 4u * (w0 & 0xFFFFFF);
			break;
		}
		case BZ_SETUP:
		{
			uint32_t k;
			bz->fb[0] = w[i + 1];
			bz->fb[1] = w[i + 2];
			bz->zaddr = w[i + 3];
			bz->W = w[i + 4] & 0xFFFF;
			bz->H = w[i + 4] >> 16;
			bz->S = (bz->W + 15) & ~15;
			for (k = 0; k < sizeof(bz->colormap); k++)
				bz->colormap[k] = rd8 (bz, w[i + 5] + k);
			bz->amap = w[i + 6];
			for (k = 0; k < sizeof(bz->pal); k++)
				bz->pal[k] = rd8 (bz, w[i + 7] + k);
			for (k = 0; k < 128; k++)
				bz->turb[k] = (int32_t)rd32 (bz, w[i + 8] + 4 * k);
			bz->draw = 0;
			set_screen_scissor (bz);
			break;
		}
		case BZ_SCISSOR:
			bz->sx0 = (int16_t)(w[i + 1] & 0xFFFF);
			bz->sy0 = (int16_t)(w[i + 1] >> 16);
			bz->sx1 = (int16_t)(w[i + 2] & 0xFFFF);
			bz->sy1 = (int16_t)(w[i + 2] >> 16);
			if (bz->sx0 < 0) bz->sx0 = 0;
			if (bz->sy0 < 0) bz->sy0 = 0;
			if (bz->sx1 > bz->W) bz->sx1 = bz->W;
			if (bz->sy1 > bz->H) bz->sy1 = bz->H;
			break;
		}
		i += len;
	}
	return 0;
}

/* ---------------------------------------------------------------- снаружи */

bz_t *bz_create (uint32_t mem_bytes)
{
	/* приведения типа — чтобы файл собирался и как C++ (эмулятор Unreal NG берёт модель себе) */
	bz_t    *bz = (bz_t *)calloc (1, sizeof(*bz));
	uint32_t size = 1;

	if (!bz)
		return NULL;
	while (size < mem_bytes)
		size <<= 1;
	bz->mem = (uint8_t *)calloc (1, size);
	if (!bz->mem)
	{
		free (bz);
		return NULL;
	}
	bz->mask = size - 1;
	return bz;
}

void bz_free (bz_t *bz)
{
	if (bz)
	{
		free (bz->mem);
		free (bz);
	}
}

const uint8_t *bz_shown (const bz_t *bz) { return bz->mem + (bz->fb[bz->draw ^ 1] & bz->mask); }
int bz_width (const bz_t *bz) { return bz->W; }
int bz_stride (const bz_t *bz) { return bz->S; }
int bz_height (const bz_t *bz) { return bz->H; }
const bz_stats_t *bz_stats (const bz_t *bz) { return &bz->st; }
uint32_t bz_mark (const bz_t *bz) { return bz->mark; }

void bz_read (const bz_t *bz, uint32_t addr, void *dst, uint32_t bytes)
{
	uint8_t *d = (uint8_t *)dst;
	uint32_t k;

	for (k = 0; k < bytes; k++)
		d[k] = rd8 (bz, addr + k);
}

uint8_t *bz_mem (bz_t *bz) { return bz->mem; }
uint32_t bz_mem_size (const bz_t *bz) { return bz->mask + 1; }
uint32_t bz_draw_fb (const bz_t *bz) { return bz->fb[bz->draw] & bz->mask; }
void bz_stats_reset (bz_t *bz) { memset (&bz->st, 0, sizeof(bz->st)); }

void bz_shown_rgb (const bz_t *bz, uint8_t *rgb)
{
	/* unreal-ng: through rd8 (the address wraps in SDRAM), not past the end of mem for a frame at its top */
	const uint32_t src = bz->fb[bz->draw ^ 1];
	const int a = bz->blend[3];
	int i, c;

	for (i = 0; i < bz->W * bz->H; i++)
		for (c = 0; c < 3; c++)
		{
			int v = bz->pal[rd8 (bz, src + (uint32_t)((i / bz->W) * bz->S + i % bz->W)) * 3 + c];
			v += (int)asr32 ((bz->blend[c] - v) * a, 8);
			rgb[i * 3 + c] = (uint8_t)v;
		}
}

void bz_set_hooks (bz_t *bz, bz_poly_hook_t poly, bz_pixel_hook_t pixel, void *user)
{
	bz->hook_poly = poly;
	bz->hook_pixel = pixel;
	bz->hook_user = user;
}

void bz_debug_clear (bz_t *bz, uint8_t color)
{
	uint32_t k;
	for (k = 0; k < (uint32_t)(bz->S * bz->H); k++)
		wr8 (bz, bz->fb[bz->draw] + k, color);
}
