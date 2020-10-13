#include "stdafx.h"
#include "loader_tap.h"

int LoaderTAP::read()
{
    int result = 0;

/*
    unsigned char *ptr = snbuf;
    close();

    while (ptr < snbuf+snapsize)
    {
        unsigned size = *(unsigned short*)ptr; ptr += 2;
        if (!size) break;
        alloc_infocell();
        desc(ptr, size, tapeinfo[tape_infosize].desc);
        tape_infosize++;
        makeblock(ptr, size, 2168, 667, 735, 855, 1710, (*ptr < 4) ? 8064 : 3220, 1000);
        ptr += size;
    }

    find_tape_sizes();
    result = (ptr == snbuf+snapsize);
*/

    return result;
}

void LoaderTAP::close()
{

}

void LoaderTAP::makeBlock(unsigned char *data, unsigned size, unsigned pilot_t,
               unsigned s1_t, unsigned s2_t, unsigned zero_t, unsigned one_t,
               unsigned pilot_len, unsigned pause, uint8_t last)
{
    //reserve(size*16 + pilot_len + 3);

    if (pilot_len != -1)
    {
        unsigned t = findPulse(pilot_t);

        for (unsigned i = 0; i < pilot_len; i++)
            tape_image[tape_imagesize++] = t;

        tape_image[tape_imagesize++] = findPulse(s1_t);
        tape_image[tape_imagesize++] = findPulse(s2_t);
    }

    unsigned t0 = findPulse(zero_t), t1 = findPulse(one_t);
    for (; size>1; size--, data++)
    {
        for (unsigned char j = 0x80; j; j >>= 1)
            tape_image[tape_imagesize++] = (*data & j) ? t1 : t0,
                    tape_image[tape_imagesize++] = (*data & j) ? t1 : t0;
    }

    // Process last byte for the block
    for (unsigned char j = 0x80; j != (unsigned char)(0x80 >> last); j >>= 1)
    {
        tape_image[tape_imagesize++] = (*data & j) ? t1 : t0,
        tape_image[tape_imagesize++] = (*data & j) ? t1 : t0;
    }

    if (pause)
    {
        tape_image[tape_imagesize++] = findPulse(pause * 3500);
    }
}

uint16_t LoaderTAP::findPulse(uint32_t t)
{
    uint16_t result;

    if (max_pulses < MAX_TAPE_PULSES)
    {
        for (unsigned i = 0; i < max_pulses; i++)
        {
            if (tape_pulse[i] == t)
                return i;
        }

        tape_pulse[max_pulses] = t;
        return max_pulses++;
    }

    if (!tape_err)
    {
        //errmsg("pulse table full");
        tape_err = 1;
    }

    uint32_t nearest = 0;
    int32_t delta = 0x7FFF'FFFF;
    for (unsigned i = 0; i < MAX_TAPE_PULSES; i++)
    {
        int32_t value = abs((int)t - (int)tape_pulse[i]);

        if (delta > value)
        {
            nearest = i;
            delta = value;
        }
    }

    return result;
}

void LoaderTAP::findTapeIndex()
{
    /*
    for (unsigned i = 0; i < tape_infosize; i++)
        if (comp.tape.play_pointer >= tape_image + tapeinfo[i].pos)
            comp.tape.index = i;
    temp.led.tape_started = -600 * 3500000;
     */
}

void LoaderTAP::findTapeSizes()
{
    /*
    for (unsigned i = 0; i < tape_infosize; i++)
    {
        unsigned end = (i == tape_infosize-1) ? tape_imagesize : tapeinfo[i+1].pos;
        unsigned sz = 0;
        for (unsigned j = tapeinfo[i].pos; j < end; j++)
        {
            sz += tape_pulse[tape_image[j]];
        }

        tapeinfo[i].t_size = sz;
    }
     */
}

void LoaderTAP::allocTapeBuffer()
{
    /*
    tapeinfo = (TAPEINFO*)realloc(tapeinfo, (tape_infosize + 1) * sizeof(TAPEINFO));
    tapeinfo[tape_infosize].pos = tape_imagesize;
    appendable = 0;
     */
}
