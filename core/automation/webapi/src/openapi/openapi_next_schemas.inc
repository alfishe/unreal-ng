    // ZX Spectrum Next report schemas (openapi_next.inc). The reports are StateNode trees: the listed members are the stable ones
    {
        struct Prop
        {
            const char* name;
            const char* type;
            const char* description;
        };
        auto fill = [&](const char* schema, const char* description, std::initializer_list<Prop> props) {
            Json::Value& s = schemas[schema];
            s["type"] = "object";
            s["description"] = description;
            for (const Prop& p : props)
            {
                s["properties"][p.name]["type"] = p.type;
                s["properties"][p.name]["description"] = p.description;
            }
        };
        fill("NextDmaReport", "The Next DMA (DeviceState::NextDma)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"mode", "string", "zxn | z80: the port of the last access (#6B | #0B)"},
              {"enabled", "boolean", "the DMA is enabled and not finished"},
              {"waiting", "boolean", "in a prescaler wait"},
              {"end_of_block", "boolean", "the end-of-block flag"},
              {"burst", "string", "continuous | burst | byte"},
              {"prescaler", "integer", "WR2 prescaler byte"},
              {"a", "object", "port A: address, type memory | io, step inc | dec | fixed, timing"},
              {"b", "object", "port B: same fields"},
              {"direction", "string", "a_to_b | b_to_a"},
              {"block_length", "integer", "WR0 block length"},
              {"counter", "integer", "bytes moved"},
              {"src", "string", "source address now, hex"},
              {"dst", "string", "destination address now, hex"},
              {"src_kind", "string", "what the 8K slot of src is (memory ports)"},
              {"dst_kind", "string", "what the 8K slot of dst is (memory ports): rom = a write into ROM"},
              {"holds_bus", "boolean", "the DMA holds the CPU"},
              {"read_mask", "string", "WR6 read mask, hex"},
              {"read_seq", "integer", "index of the value the next read returns"},
              {"status", "string", "the status byte a read returns when read_seq is 0, hex"},
              {"interrupt_enables", "object", "nr_cc, nr_cd, nr_ce"}});
        fill("NextVideoReport", "The Next video layers (DeviceState::NextVideo)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"layer_order", "object", "value (NR #15 bits 4:2), name (SLU LSU SUL LUS USL ULS blend)"},
              {"ula", "object", "enabled, mode, port_ff, shadow, scroll_x / y, clip, palette, ulanext"},
              {"layer2", "object", "enabled, resolution, bank, shadow_bank, palette_offset, scroll, clip, cpu_map"},
              {"tilemap", "object", "enabled, columns, attributes_in_map, text, mode512, on_top, map_bank / map_offset, tile_bank / tile_offset, scroll, clip"},
              {"sprites", "object", "enabled, over_border, clip_over_border, zero_on_top, clip"},
              {"transparency", "object", "global, fallback, sprites"},
              {"raster", "object", "frame, frame_t, hc, vc, tstates_per_line, int_line, current_line"},
              {"timing", "object", "family, hz"},
              {"layer_capture", "object", "the per-layer record of the screen: enabled, frames, last_frame, bytes (POST /next/layer-capture)"}});
        fill("NextLayerCaptureState", "The per-layer record of the Next screen (DeviceState::NextLayerCaptureControl)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"enabled", "boolean", "the screen records the layers of every frame (from the next frame start)"},
              {"frames", "integer", "frames completed since it was switched on"},
              {"last_frame", "integer", "the emulator frame counter of the last completed frame (0 before the first)"},
              {"bytes", "integer", "memory held by the two sets of buffers"}});
        fill("NextPixelReport", "One pixel of the last captured Next frame (DeviceState::NextPixel)",
             {{"ok", "boolean", "true"},
              {"frame", "object", "sequence, frame_number of the frame the answer is from"},
              {"pixel", "object", "x, y, subpixel, subpixel_x, segment_from_x / segment_to_x (the line segment the pixel was drawn in)"},
              {"winner", "object", "layer (fallback ula border layer2 tilemap sprite blend stencil) and id"},
              {"colour", "object", "the composed colour: rgb9, rgb"},
              {"layer_order", "string", "SLU LSU SUL LUS USL ULS or blend (NR #15 bits 4:2 of the segment)"},
              {"layers", "object", "ula, layer2, tilemap, sprites: each {opaque, rgb9, rgb, wins, why, source}"}});
        fill("NextLayerLegendEntry", "One winner id of the ids image",
             {{"id", "integer", "0 fallback, 1 ula, 2 border, 3 layer2, 4 tilemap, 5 sprite, 6 blend (layer order 110 / 111), 7 stencil"},
              {"name", "string", "the name of the id"},
              {"colour", "string", "the colour of that id in the ids image, #RRGGBB"}});
        fill("NextLayers", "All layers of one Next frame (GET /capture/next-layers)",
             {{"status", "string", "success"},
              {"format", "string", "png | rgba"},
              {"alpha", "boolean", "true: alpha 0 where a layer does not paint"},
              {"width", "integer", "640"},
              {"height", "integer", "512"},
              {"frame", "object", "sequence (the n-th frame recorded), frame_number (the emulator frame counter)"},
              {"layers", "object", "ula, layer2, tilemap, sprites, ids: each {size, data (base64)}"},
              {"legend", "array", "the ids image legend: id, name, colour"}});
        fill("NextCopperReport", "The Next copper (DeviceState::NextCopper)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"control", "object", "mode (0-3), name, raw_nr_62"},
              {"running", "boolean", "mode is not stopped"},
              {"address", "integer", "the write address, byte granular 0-2047"},
              {"word_index", "integer", "address / 2"},
              {"address_byte", "string", "msb | lsb: which half of a word the next NR #60 write stores"},
              {"pc", "integer", "the instruction the copper is at (0-1023)"},
              {"line_offset", "integer", "NR #64"},
              {"list_length", "integer", "index of the last non-zero word + 1"},
              {"instructions", "array", "index, word, op, reg, reg_name, value | hpos, vpos, text, current"},
              {"around_pc", "array", "the words pc-3 .. pc+4 when the pc is outside the window"},
              {"raw", "string", "the 2048 bytes as hex (raw=true)"}});
        fill("NextSpritesReport", "The Next sprites (DeviceState::NextSprites)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"enabled", "boolean", "NR #15 bit 0"},
              {"over_border", "boolean", "NR #15 bit 1"},
              {"clip", "object", "x1, x2, y1, y2 (NR #19)"},
              {"flags", "object", "collision, too_many (not cleared by the report)"},
              {"visible_count", "integer", "visible sprites of all 128"},
              {"sprites", "array", "index, kind, x, y, pattern, four_bit, palette_offset, x_mirror, y_mirror, rotate, scale_x, scale_y, visible, bytes (+ anchor, offset_x / y for a relative sprite)"},
              {"pattern_memory", "object", "size, non_zero_bytes, used_patterns_8bit, used[]"}});
        fill("NextPaletteReport", "The Next palettes (DeviceState::NextPalette)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"selected", "object", "palette, name, index, auto_increment (NR #43 / #40)"},
              {"transparent", "object", "global, sprites, tilemap, fallback"},
              {"palettes", "array", "per palette: palette, name, entries[index, rgb9, red, green, blue, priority]"}});
        fill("NextPortsReport", "The Next port decode (DeviceState::NextPorts)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"describe", "object", "port, access, device, decoded_by, enabled, enabled_by, enforced, side_effect (only with `port`)"},
              {"enable_word", "object", "value (32 bit hex), nr_82..nr_85, bits[bit, nr, ports, enabled, enforced]"}});
        fill("NextRegReadReport", "A NextREG read (DeviceState::NextRegRead)",
             {{"available", "boolean", "false on a machine that is not a Next"},
              {"reg", "string", "register number, hex (one register)"},
              {"name", "string", "register name"},
              {"value", "string", "what a read returns, hex"},
              {"stored", "string", "the stored byte, hex"},
              {"reset", "string", "reset value, hex"},
              {"decoded", "string", "decoded bits of NR #02 / #03 / #07"},
              {"registers", "array", "every register: nr, name, access, value, stored, reset (no `reg`)"}});
        fill("NextRegWriteResult", "A NextREG write (NextRegWriteControl)",
             {{"ok", "boolean", "the write was done"},
              {"reg", "string", "register, hex"},
              {"value", "string", "value written, hex"},
              {"previous", "string", "the stored byte before, hex"},
              {"after", "string", "what a read of the register returns now, hex"},
              {"door", "string", "nextreg | port | internal"},
              {"moment", "string", "paused | stopped | frame"}});
    }
