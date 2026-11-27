/*
 * =====================================================================================================
 * bomzh.h — что C-модель «бомж-3D» даёт снаружи: команды, режимы, счётчики, функции.
 * =====================================================================================================
 *
 * ЗАЧЕМ ЗАГОЛОВОЧНЫЙ ФАЙЛ
 *   В C программа делится на файлы; .h — «витрина» модуля: какие в нём типы и функции и как их звать.
 *   Модель (bomzh.c) нужна трём программам: переднему краю Quake 2 (gpu/front/bz_front.c — шлёт
 *   команды и сравнивает кадры), стенду Verilog (gpu/rtl/tb/tb_bz.cpp — сверяет RTL с моделью) и в
 *   будущем — коду ESP (тем же кодированием команд). Все они включают этот файл.
 *
 * ЧТО ВНУТРИ
 *   коды команд BZ_* — старший байт первого слова команды (как в SPEC.md);
 *   биты режима многоугольника BZ_M_* (перспектива, свет, z-тест, небо, волна, прозрачность, ключ);
 *   bz_stats_t — счётчики работы (сколько точек, промахов кэша, пачек SDRAM) для оценки скорости;
 *   bz_create / bz_exec / bz_shown — создать модель, выполнить поток команд, взять показанный кадр.
 *
 * Модель — «железо на бумаге»: она получает тот же поток 32-битных слов, что ПЛИС получит по LINK от
 * ESP, и рисует в свою копию SDRAM ровно те точки, которые нарисует Verilog. Поэтому в ней нет
 * плавающей точки — только целые числа той ширины, что будет в RTL.
 */
#ifndef BOMZH_H
#define BOMZH_H

#include <stdint.h>

/* MSVC в режиме C знает inline только как __inline */
#if defined(_MSC_VER) && !defined(__cplusplus)
#define BZ_INLINE __inline
#else
#define BZ_INLINE inline
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Коды команд: старший байт первого слова (SPEC.md, «Команды»). */
enum {
	BZ_TEX = 0x01, BZ_LMAP = 0x02, BZ_POLY = 0x03, BZ_POINT = 0x04, BZ_PIC = 0x05,
	BZ_FILL = 0x06, BZ_FRAME = 0x07, BZ_TIME = 0x08, BZ_WRITE = 0x09, BZ_SETUP = 0x0A,
	BZ_SCISSOR = 0x0B, BZ_VBUF = 0x0C, BZ_STBUF = 0x0D, BZ_TRIS = 0x0E,
	BZ_MARK = 0x0F       /* SPEC 0.4: число в регистр MARK — ESP так видит, что всё до него выполнено */
};

/* Режим многоугольника: биты 8…23 первого слова POLY, здесь — уже сдвинутые к нулю. */
enum {
	BZ_M_PERSP  = 1 << 0,   /* u, v — это s·zi и t·zi (перспектива), иначе s, t (аффинно)   */
	BZ_M_LIGHT  = 3 << 1,   /* поле света: */
	BZ_L_LMAP   = 0 << 1,   /*   по lightmap (билинейно в каждой точке)                     */
	BZ_L_VERT   = 1 << 1,   /*   по вершинам: четвёртый градиент l                          */
	BZ_L_NONE   = 2 << 1,   /*   без света: тексель идёт в кадр как есть                    */
	BZ_M_ZTEST  = 1 << 3,   /* z-тест                                                       */
	BZ_M_ZWRITE = 1 << 4,   /* запись z                                                     */
	BZ_M_SKY    = 1 << 5,   /* небо: z = 0, рисуется только там, где z ещё не писали        */
	BZ_M_WARP   = 1 << 6,   /* волна воды Quake                                             */
	BZ_M_TRANS  = 3 << 7,   /* поле прозрачности: */
	BZ_T_33     = 1 << 7,   /*   33 % (trans33 Quake)                                       */
	BZ_T_66     = 2 << 7,   /*   66 % (trans66 Quake)                                       */
	BZ_M_KEY    = 1 << 9,   /* тексель 255 прозрачен                                        */
	BZ_M_ZGT    = 1 << 10   /* z-тест строгий (>): для мира, который идёт спереди назад     */
};

/* Счётчики работы — копятся, пока их не обнулят (bz_stats_reset). */
typedef struct
{
	uint64_t cmd_words;        /* слов команд (без данных WRITE)                         */
	uint64_t write_bytes;      /* байт данных WRITE (загрузки текстур, lightmap и т. п.)  */
	uint64_t polys, points, pics, fills, frames;
	uint64_t tris, tris_culled;       /* треугольники TRIS: всего и отброшенных (d ≤ 0)           */
	uint64_t px_raster;        /* точек внутри многоугольников (после ножниц)            */
	uint64_t px_zfail;         /* отброшено z-тестом                                      */
	uint64_t px_key;           /* отброшено ключом 255                                    */
	uint64_t px_written;       /* записано в кадр (многоугольники, частицы, картинки)     */
	uint64_t px_fill;          /* записано заливкой (FILL)                                */
	uint64_t tex_reads;        /* чтений текселя                                          */
	uint64_t tex_miss;         /* промахов кэша текстур (строка 64 байта из SDRAM)        */
	uint64_t lm_fetch;         /* выборок 2×2 клеток lightmap (смена клетки)              */
	uint64_t divides;          /* делений 2^46 / zi (концы кусков)                        */
	uint64_t seg;              /* кусков строки (до 16 точек, выровнены по 16)            */
	uint64_t seg_zread;        /* кусков с чтением z пачкой (32 байта)                    */
	uint64_t seg_zwrite;       /* кусков с записью z пачкой                               */
	uint64_t seg_cread;        /* кусков с чтением цвета (прозрачность)                   */
	uint64_t seg_cwrite;       /* кусков с записью цвета (16 байт)                        */
} bz_stats_t;

typedef struct bz_s bz_t;

/* Модель с SDRAM заданного размера (байт; на плате — 8 МБ). Память обнулена. */
bz_t *bz_create (uint32_t mem_bytes);
void  bz_free (bz_t *bz);

/* Выполнить поток команд. 0 — всё разобрано; иначе номер слова (с 1), где встретилась ошибка. */
uint32_t bz_exec (bz_t *bz, const uint32_t *words, uint32_t count);

/* Показанный кадр (после последнего FRAME): индексы палитры, строки с шагом bz_stride. */
const uint8_t *bz_shown (const bz_t *bz);
/* Тот же кадр в RGB (3 байта на точку): палитра и вспышка FRAME, как на выводе HDMI. */
void bz_shown_rgb (const bz_t *bz, uint8_t *rgb);
int  bz_width (const bz_t *bz);
int  bz_height (const bz_t *bz);
int  bz_stride (const bz_t *bz);  /* шаг строки кадра: W вверх до 16 */

/* Только для стенда (в железе этого нет): залить рисуемый кадр цветом, не считая работу. */
void bz_debug_clear (bz_t *bz, uint8_t color);

/* Крючки для стендов RTL по частям (этап B, gpu/rtl/tb/tb_raster.cpp). poly — многоугольник на входе
 * растеризатора (вершины Q12.4, режим, градиенты zi, u, v, l по 3 слова, ножницы); pixel — каждая его
 * точка в порядке выдачи (строки сверху вниз, слева направо), до z-теста: zi, s и t (у перспективы — после
 * деления, до волны), l. Модель от крючков не меняется; NULL — выключено. */
typedef void (*bz_poly_hook_t) (void *user, const uint32_t *xy, int n, int mode, const int32_t g[4][3],
                                int sx0, int sy0, int sx1, int sy1);
typedef void (*bz_pixel_hook_t) (void *user, int x, int y, int32_t zi, int32_t s, int32_t t, int32_t l);
void bz_set_hooks (bz_t *bz, bz_poly_hook_t poly, bz_pixel_hook_t pixel, void *user);

/* SPEC 0.4, чтение SDRAM по LINK. bz_mark — значение последней выполненной команды MARK (регистр MARK
 * ядра). bz_read — bytes байт SDRAM с адреса addr (как их отдаст окно чтения ядра: младший байт слова
 * первым); адрес и длину ядро берёт кратными 32 — здесь любые. */
uint32_t bz_mark (const bz_t *bz);
void     bz_read (const bz_t *bz, uint32_t addr, void *dst, uint32_t bytes);

/* Только для стенда RTL (gpu/rtl/tb): память модели и адрес рисуемого кадра. */
uint8_t *bz_mem (bz_t *bz);
uint32_t bz_mem_size (const bz_t *bz);
uint32_t bz_draw_fb (const bz_t *bz);

const bz_stats_t *bz_stats (const bz_t *bz);
void bz_stats_reset (bz_t *bz);

/* Помощники для переднего края: кодирование команд и чисел. */
static BZ_INLINE uint32_t bz_xy (int x, int y) { return (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16); }
/* z в 16 бит из zi (Q2.30): «плавающее» 5+11, больше — ближе (SPEC.md, «Точка»). */
uint16_t bz_zenc (int32_t zi);

#ifdef __cplusplus
}
#endif

#endif
