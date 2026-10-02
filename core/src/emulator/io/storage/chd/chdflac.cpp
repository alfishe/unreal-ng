#include "stdafx.h"

#include "chdflac.h"

#include <algorithm>
#include <array>

namespace chd::flac
{
    namespace
    {
        /// region <CRCs>

        struct CrcTables
        {
            uint8_t crc8[256];
            uint16_t crc16[256];
            CrcTables()
            {
                for (uint32_t i = 0; i < 256; i++)
                {
                    uint32_t c8 = i;
                    for (int b = 0; b < 8; b++)
                        c8 = (c8 & 0x80) ? ((c8 << 1) ^ 0x07) : (c8 << 1);
                    crc8[i] = static_cast<uint8_t>(c8);
                    uint32_t c16 = i << 8;
                    for (int b = 0; b < 8; b++)
                        c16 = (c16 & 0x8000) ? ((c16 << 1) ^ 0x8005) : (c16 << 1);
                    crc16[i] = static_cast<uint16_t>(c16);
                }
            }
        };

        const CrcTables& Tables()
        {
            static const CrcTables tables;
            return tables;
        }

        uint8_t Crc8(const uint8_t* data, size_t length)
        {
            uint8_t crc = 0;
            for (size_t i = 0; i < length; i++)
                crc = Tables().crc8[crc ^ data[i]];
            return crc;
        }

        uint16_t Crc16(const uint8_t* data, size_t length)
        {
            uint16_t crc = 0;
            for (size_t i = 0; i < length; i++)
                crc = static_cast<uint16_t>((crc << 8) ^ Tables().crc16[((crc >> 8) ^ data[i]) & 0xFF]);
            return crc;
        }

        /// endregion </CRCs>

        /// region <Bit I/O>

        class Writer
        {
        public:
            explicit Writer(std::vector<uint8_t>& out) : _out(out) {}

            void Put(uint32_t value, int bits)
            {
                if (bits == 0)
                    return;
                const uint64_t mask = (bits >= 32) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1);
                _acc = (_acc << bits) | (value & mask);
                _count += bits;
                while (_count >= 8)
                {
                    _out.push_back(static_cast<uint8_t>(_acc >> (_count - 8)));
                    _count -= 8;
                }
            }
            void PutSigned(int32_t value, int bits) { Put(static_cast<uint32_t>(value), bits); }
            void PutUnary(uint32_t zeros)
            {
                while (zeros >= 24)
                {
                    Put(0, 24);
                    zeros -= 24;
                }
                Put(1, static_cast<int>(zeros) + 1);
            }
            void Align()
            {
                if (_count)
                    Put(0, 8 - _count);
            }

        private:
            std::vector<uint8_t>& _out;
            uint64_t _acc = 0;
            int _count = 0;
        };

        class Reader
        {
        public:
            Reader(const uint8_t* data, size_t length) : _data(data), _bits(length * 8) {}

            uint32_t Get(int bits)
            {
                uint32_t value = 0;
                while (bits > 0)
                {
                    if (_pos >= _bits)
                    {
                        _bad = true;
                        return 0;
                    }
                    const int available = 8 - static_cast<int>(_pos & 7);
                    const int take = std::min(available, bits);
                    const uint32_t byte = _data[_pos >> 3];
                    value = (value << take) | ((byte >> (available - take)) & ((1u << take) - 1));
                    _pos += static_cast<size_t>(take);
                    bits -= take;
                }
                return value;
            }
            int32_t GetSigned(int bits)
            {
                if (bits == 0)
                    return 0;
                const uint32_t raw = Get(bits);
                if (bits >= 32)
                    return static_cast<int32_t>(raw);
                const uint32_t sign = 1u << (bits - 1);
                return static_cast<int32_t>((raw ^ sign)) - static_cast<int32_t>(sign);
            }
            uint32_t Unary()
            {
                uint32_t zeros = 0;
                while (true)
                {
                    if (_pos >= _bits)
                    {
                        _bad = true;
                        return 0;
                    }
                    // Whole zero bytes at once
                    if ((_pos & 7) == 0 && _data[_pos >> 3] == 0)
                    {
                        zeros += 8;
                        _pos += 8;
                        continue;
                    }
                    if (Get(1))
                        return zeros;
                    zeros++;
                }
            }
            void Align() { _pos = (_pos + 7) & ~static_cast<size_t>(7); }
            size_t BytePos() const { return _pos >> 3; }
            bool AtEnd() const { return _pos + 8 > _bits; }
            bool Bad() const { return _bad || _pos > _bits; }

        private:
            const uint8_t* _data;
            size_t _bits;
            size_t _pos = 0;
            bool _bad = false;
        };

        /// endregion </Bit I/O>

        /// region <Encoder>

        constexpr int kMaxPartitionOrder = 8;

        uint32_t ZigZag(int32_t v)
        {
            return (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
        }

        struct ResidualPlan
        {
            int partitionOrder = 0;
            bool rice2 = false;
            std::vector<uint8_t> params;
            uint64_t bits = 0;
        };

        struct SubframePlan
        {
            enum class Type
            {
                Constant,
                Verbatim,
                Fixed
            } type = Type::Verbatim;
            int order = 0;
            ResidualPlan residual;
            uint64_t bits = 0;
        };

        /// Best Rice parameter for `count` values summing to `sum`, with its estimated cost
        uint64_t BestRice(uint64_t sum, uint32_t count, uint8_t& param)
        {
            uint64_t best = ~0ULL;
            param = 0;
            for (uint8_t k = 0; k <= 30; k++)
            {
                const uint64_t cost = static_cast<uint64_t>(count) * (k + 1) + (sum >> k);
                if (cost < best)
                {
                    best = cost;
                    param = k;
                }
                if ((sum >> k) == 0)
                    break;
            }
            return best;
        }

        /// The residual of `order` for x[order..n): the partitioning with the fewest bits
        ResidualPlan PlanResidual(const std::vector<uint32_t>& folded, uint32_t n, int order)
        {
            int maxOrder = 0;
            while (maxOrder < kMaxPartitionOrder && (n % (2u << maxOrder)) == 0 && (n >> (maxOrder + 1)) > static_cast<uint32_t>(order))
                maxOrder++;

            // Sums per partition at the finest order, folded up level by level
            const uint32_t finest = 1u << maxOrder;
            std::vector<uint64_t> sums(finest, 0);
            const uint32_t length = n >> maxOrder;
            for (uint32_t p = 0; p < finest; p++)
            {
                const uint32_t from = p == 0 ? static_cast<uint32_t>(order) : p * length;
                for (uint32_t i = from; i < (p + 1) * length; i++)
                    sums[p] += folded[i];
            }

            ResidualPlan best;
            best.bits = ~0ULL;
            for (int po = maxOrder; po >= 0; po--)
            {
                const uint32_t parts = 1u << po;
                const uint32_t partLength = n >> po;
                ResidualPlan plan;
                plan.partitionOrder = po;
                plan.params.resize(parts);
                uint64_t bits = 6;
                bool rice2 = false;
                for (uint32_t p = 0; p < parts; p++)
                {
                    const uint32_t count = partLength - (p == 0 ? static_cast<uint32_t>(order) : 0);
                    bits += BestRice(sums[p], count, plan.params[p]);
                    rice2 = rice2 || plan.params[p] > 14;
                }
                bits += static_cast<uint64_t>(parts) * (rice2 ? 5 : 4);
                plan.rice2 = rice2;
                plan.bits = bits;
                if (bits < best.bits)
                    best = plan;
                // Fold to the next coarser level
                if (po > 0)
                {
                    for (uint32_t p = 0; p < parts / 2; p++)
                        sums[p] = sums[2 * p] + sums[2 * p + 1];
                }
            }
            return best;
        }

        void FixedResidual(const int32_t* x, uint32_t n, int order, std::vector<int32_t>& residual)
        {
            residual.assign(n, 0);
            for (uint32_t i = static_cast<uint32_t>(order); i < n; i++)
            {
                switch (order)
                {
                    case 0: residual[i] = x[i]; break;
                    case 1: residual[i] = x[i] - x[i - 1]; break;
                    case 2: residual[i] = x[i] - 2 * x[i - 1] + x[i - 2]; break;
                    case 3: residual[i] = x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3]; break;
                    default: residual[i] = x[i] - 4 * x[i - 1] + 6 * x[i - 2] - 4 * x[i - 3] + x[i - 4]; break;
                }
            }
        }

        SubframePlan PlanSubframe(const int32_t* x, uint32_t n, int bps)
        {
            SubframePlan best;
            best.type = SubframePlan::Type::Verbatim;
            best.bits = 8 + static_cast<uint64_t>(n) * static_cast<uint64_t>(bps);

            bool constant = true;
            for (uint32_t i = 1; i < n && constant; i++)
                constant = x[i] == x[0];
            if (constant)
            {
                best.type = SubframePlan::Type::Constant;
                best.bits = 8 + static_cast<uint64_t>(bps);
                return best;
            }

            std::vector<int32_t> residual;
            std::vector<uint32_t> folded(n);
            for (int order = 0; order <= 4 && static_cast<uint32_t>(order) < n; order++)
            {
                FixedResidual(x, n, order, residual);
                for (uint32_t i = 0; i < n; i++)
                    folded[i] = i < static_cast<uint32_t>(order) ? 0 : ZigZag(residual[i]);
                ResidualPlan plan = PlanResidual(folded, n, order);
                const uint64_t bits = 8 + static_cast<uint64_t>(order) * static_cast<uint64_t>(bps) + plan.bits;
                if (bits < best.bits)
                {
                    best.type = SubframePlan::Type::Fixed;
                    best.order = order;
                    best.residual = std::move(plan);
                    best.bits = bits;
                }
            }
            return best;
        }

        void WriteSubframe(Writer& w, const int32_t* x, uint32_t n, int bps, const SubframePlan& plan)
        {
            switch (plan.type)
            {
                case SubframePlan::Type::Constant:
                    w.Put(0x00, 8);  // pad, type 000000, no wasted bits
                    w.PutSigned(x[0], bps);
                    return;
                case SubframePlan::Type::Verbatim:
                    w.Put(0x02, 8);  // type 000001
                    for (uint32_t i = 0; i < n; i++)
                        w.PutSigned(x[i], bps);
                    return;
                case SubframePlan::Type::Fixed:
                    break;
            }

            w.Put(static_cast<uint32_t>((0x08 + plan.order) << 1), 8);  // type 001xxx
            for (int i = 0; i < plan.order; i++)
                w.PutSigned(x[i], bps);

            std::vector<int32_t> residual;
            FixedResidual(x, n, plan.order, residual);
            const ResidualPlan& r = plan.residual;
            w.Put(r.rice2 ? 1 : 0, 2);
            w.Put(static_cast<uint32_t>(r.partitionOrder), 4);
            const uint32_t parts = 1u << r.partitionOrder;
            const uint32_t partLength = n >> r.partitionOrder;
            for (uint32_t p = 0; p < parts; p++)
            {
                const uint8_t k = r.params[p];
                w.Put(k, r.rice2 ? 5 : 4);
                const uint32_t from = p == 0 ? static_cast<uint32_t>(plan.order) : p * partLength;
                for (uint32_t i = from; i < (p + 1) * partLength; i++)
                {
                    const uint32_t u = ZigZag(residual[i]);
                    w.PutUnary(u >> k);
                    w.Put(u, k);
                }
            }
        }

        int BlockSizeCode(uint32_t n)
        {
            if (n == 192)
                return 1;
            for (int c = 2; c <= 5; c++)
            {
                if (n == (576u << (c - 2)))
                    return c;
            }
            for (int c = 8; c <= 15; c++)
            {
                if (n == (256u << (c - 8)))
                    return c;
            }
            return n <= 256 ? 6 : 7;
        }

        void PutUtf8(Writer& w, uint32_t value)
        {
            if (value < 0x80)
            {
                w.Put(value, 8);
                return;
            }
            int extra = value < 0x800 ? 1 : value < 0x10000 ? 2 : value < 0x200000 ? 3 : value < 0x4000000 ? 4 : 5;
            const uint32_t leadMarks = (0xFF00u >> (extra + 1)) & 0xFF;
            w.Put(leadMarks | (value >> (6 * extra)), 8);
            for (int i = extra - 1; i >= 0; i--)
                w.Put(0x80 | ((value >> (6 * i)) & 0x3F), 8);
        }
        /// endregion </Encoder>
    }  // namespace

    void Encode(const int16_t* samples, uint32_t frames, int channels, uint32_t blockSize, std::vector<uint8_t>& out)
    {
        Writer w(out);
        std::array<std::vector<int32_t>, 8> input;
        uint32_t frameNumber = 0;
        for (uint32_t first = 0; first < frames; first += blockSize, frameNumber++)
        {
            const uint32_t n = std::min(blockSize, frames - first);
            for (int c = 0; c < channels; c++)
            {
                input[c].resize(n);
                for (uint32_t i = 0; i < n; i++)
                    input[c][i] = samples[(first + i) * static_cast<uint32_t>(channels) + static_cast<uint32_t>(c)];
            }

            // Channel assignment: independent, or the best stereo decorrelation
            int assignment = channels - 1;
            const int32_t* sources[8] = {};
            int bits[8] = {};
            SubframePlan plans[8];
            std::vector<int32_t> side;
            std::vector<int32_t> mid;
            for (int c = 0; c < channels; c++)
            {
                sources[c] = input[c].data();
                bits[c] = 16;
                plans[c] = PlanSubframe(sources[c], n, 16);
            }
            if (channels == 2)
            {
                side.resize(n);
                mid.resize(n);
                for (uint32_t i = 0; i < n; i++)
                {
                    side[i] = input[0][i] - input[1][i];
                    mid[i] = (input[0][i] + input[1][i]) >> 1;
                }
                const SubframePlan sidePlan = PlanSubframe(side.data(), n, 17);
                const SubframePlan midPlan = PlanSubframe(mid.data(), n, 16);
                const uint64_t independent = plans[0].bits + plans[1].bits;
                const uint64_t leftSide = plans[0].bits + sidePlan.bits;
                const uint64_t sideRight = sidePlan.bits + plans[1].bits;
                const uint64_t midSide = midPlan.bits + sidePlan.bits;
                const uint64_t best = std::min({independent, leftSide, sideRight, midSide});
                if (best == midSide && best < independent)
                {
                    assignment = 10;
                    sources[0] = mid.data();
                    plans[0] = midPlan;
                    sources[1] = side.data();
                    bits[1] = 17;
                    plans[1] = sidePlan;
                }
                else if (best == leftSide && best < independent)
                {
                    assignment = 8;
                    sources[1] = side.data();
                    bits[1] = 17;
                    plans[1] = sidePlan;
                }
                else if (best == sideRight && best < independent)
                {
                    assignment = 9;
                    sources[0] = side.data();
                    bits[0] = 17;
                    plans[0] = sidePlan;
                }
            }

            // Frame header
            const size_t start = out.size();
            const int bsCode = BlockSizeCode(n);
            w.Put(0x3FFE, 14);
            w.Put(0, 1);  // reserved
            w.Put(0, 1);  // fixed block size: the coded number is the frame number
            w.Put(static_cast<uint32_t>(bsCode), 4);
            w.Put(9, 4);  // 44.1 kHz, as MAME's encoder says
            w.Put(static_cast<uint32_t>(assignment), 4);
            w.Put(4, 3);  // 16 bits per sample
            w.Put(0, 1);
            PutUtf8(w, frameNumber);
            if (bsCode == 6)
                w.Put(n - 1, 8);
            else if (bsCode == 7)
                w.Put(n - 1, 16);
            w.Put(Crc8(out.data() + start, out.size() - start), 8);

            for (int c = 0; c < channels; c++)
                WriteSubframe(w, sources[c], n, bits[c], plans[c]);
            w.Align();
            w.Put(Crc16(out.data() + start, out.size() - start), 16);
        }
    }

    /// region <Decoder>

    namespace
    {
        bool DecodeResidual(Reader& r, uint32_t n, int order, int32_t* out)
        {
            const uint32_t method = r.Get(2);
            if (method > 1)
                return false;
            const int paramBits = method == 0 ? 4 : 5;
            const uint32_t escape = method == 0 ? 15u : 31u;
            const uint32_t partitionOrder = r.Get(4);
            const uint32_t parts = 1u << partitionOrder;
            const uint32_t partLength = n >> partitionOrder;
            if ((partLength << partitionOrder) != n || partLength < static_cast<uint32_t>(order))
                return false;
            uint32_t at = static_cast<uint32_t>(order);
            for (uint32_t p = 0; p < parts; p++)
            {
                const uint32_t count = partLength - (p == 0 ? static_cast<uint32_t>(order) : 0);
                const uint32_t k = r.Get(paramBits);
                if (k == escape)
                {
                    const int raw = static_cast<int>(r.Get(5));
                    for (uint32_t i = 0; i < count; i++)
                        out[at++] = r.GetSigned(raw);
                }
                else
                {
                    for (uint32_t i = 0; i < count; i++)
                    {
                        const uint32_t q = r.Unary();
                        const uint32_t u = (q << k) | r.Get(static_cast<int>(k));
                        out[at++] = static_cast<int32_t>(u >> 1) ^ -static_cast<int32_t>(u & 1);
                    }
                }
                if (r.Bad())
                    return false;
            }
            return true;
        }

        bool DecodeSubframe(Reader& r, uint32_t n, int bps, int32_t* out)
        {
            if (r.Get(1) != 0)
                return false;
            const uint32_t type = r.Get(6);
            int wasted = 0;
            if (r.Get(1))
                wasted = static_cast<int>(r.Unary()) + 1;
            if (wasted >= bps)
                return false;
            bps -= wasted;

            if (type == 0)
            {
                const int32_t value = r.GetSigned(bps);
                std::fill(out, out + n, value);
            }
            else if (type == 1)
            {
                for (uint32_t i = 0; i < n; i++)
                    out[i] = r.GetSigned(bps);
            }
            else if (type >= 8 && type <= 12)
            {
                const int order = static_cast<int>(type - 8);
                if (static_cast<uint32_t>(order) > n)
                    return false;
                for (int i = 0; i < order; i++)
                    out[i] = r.GetSigned(bps);
                if (!DecodeResidual(r, n, order, out))
                    return false;
                for (uint32_t i = static_cast<uint32_t>(order); i < n; i++)
                {
                    switch (order)
                    {
                        case 0: break;
                        case 1: out[i] += out[i - 1]; break;
                        case 2: out[i] += 2 * out[i - 1] - out[i - 2]; break;
                        case 3: out[i] += 3 * out[i - 1] - 3 * out[i - 2] + out[i - 3]; break;
                        default: out[i] += 4 * out[i - 1] - 6 * out[i - 2] + 4 * out[i - 3] - out[i - 4]; break;
                    }
                }
            }
            else if (type >= 32)
            {
                const int order = static_cast<int>(type - 31);
                if (static_cast<uint32_t>(order) > n)
                    return false;
                for (int i = 0; i < order; i++)
                    out[i] = r.GetSigned(bps);
                const uint32_t precisionCode = r.Get(4);
                if (precisionCode == 15)
                    return false;
                const int precision = static_cast<int>(precisionCode) + 1;
                const int shift = r.GetSigned(5);
                if (shift < 0)
                    return false;
                int32_t coefficients[32];
                for (int i = 0; i < order; i++)
                    coefficients[i] = r.GetSigned(precision);
                if (!DecodeResidual(r, n, order, out))
                    return false;
                for (uint32_t i = static_cast<uint32_t>(order); i < n; i++)
                {
                    int64_t sum = 0;
                    for (int j = 0; j < order; j++)
                        sum += static_cast<int64_t>(coefficients[j]) * out[i - 1 - static_cast<uint32_t>(j)];
                    out[i] += static_cast<int32_t>(sum >> shift);
                }
            }
            else
            {
                return false;
            }

            if (wasted)
            {
                for (uint32_t i = 0; i < n; i++)
                    out[i] = static_cast<int32_t>(static_cast<uint32_t>(out[i]) << wasted);
            }
            return !r.Bad();
        }
    }  // namespace

    bool Decode(const uint8_t* src, size_t length, int16_t* samples, uint32_t frames, int channels)
    {
        if (channels < 1 || channels > 8)
            return false;
        Reader r(src, length);
        std::array<std::vector<int32_t>, 8> decoded;
        uint32_t produced = 0;
        while (produced < frames)
        {
            r.Align();
            const size_t start = r.BytePos();
            if (r.Get(14) != 0x3FFE)
                return false;
            r.Get(1);  // reserved
            r.Get(1);  // blocking strategy
            const uint32_t bsCode = r.Get(4);
            const uint32_t srCode = r.Get(4);
            const uint32_t assignment = r.Get(4);
            const uint32_t ssCode = r.Get(3);
            r.Get(1);

            // The frame or sample number, UTF-8 coded
            const uint32_t lead = r.Get(8);
            int extra = 0;
            if (lead >= 0xC0)
            {
                extra = lead >= 0xFE ? 6 : lead >= 0xFC ? 5 : lead >= 0xF8 ? 4 : lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : 1;
            }
            else if (lead >= 0x80)
            {
                return false;
            }
            for (int i = 0; i < extra; i++)
            {
                if ((r.Get(8) & 0xC0) != 0x80)
                    return false;
            }

            uint32_t n = 0;
            if (bsCode == 0)
                return false;
            else if (bsCode == 1)
                n = 192;
            else if (bsCode <= 5)
                n = 576u << (bsCode - 2);
            else if (bsCode == 6)
                n = r.Get(8) + 1;
            else if (bsCode == 7)
                n = r.Get(16) + 1;
            else
                n = 256u << (bsCode - 8);

            if (srCode == 12)
                r.Get(8);
            else if (srCode == 13 || srCode == 14)
                r.Get(16);
            else if (srCode == 15)
                return false;

            static const int kSampleSizes[8] = {16, 8, 12, 0, 16, 20, 24, 32};  // 0: from STREAMINFO (16 for CHD)
            const int bps = kSampleSizes[ssCode];
            if (bps == 0)
                return false;

            const size_t headerEnd = r.BytePos();
            const uint32_t headerCrc = r.Get(8);
            if (r.Bad() || headerCrc != Crc8(src + start, headerEnd - start))
                return false;

            int frameChannels = 0;
            if (assignment < 8)
                frameChannels = static_cast<int>(assignment) + 1;
            else if (assignment <= 10)
                frameChannels = 2;
            else
                return false;
            if (frameChannels != channels)
                return false;

            for (int c = 0; c < channels; c++)
            {
                decoded[c].resize(n);
                const bool sideChannel = (assignment == 8 && c == 1) || (assignment == 9 && c == 0) || (assignment == 10 && c == 1);
                if (!DecodeSubframe(r, n, bps + (sideChannel ? 1 : 0), decoded[c].data()))
                    return false;
            }
            r.Align();
            const size_t frameEnd = r.BytePos();
            const uint32_t frameCrc = r.Get(16);
            if (r.Bad() || frameCrc != Crc16(src + start, frameEnd - start))
                return false;

            for (uint32_t i = 0; i < n && produced < frames; i++, produced++)
            {
                int32_t left = decoded[0][i];
                int32_t right = channels > 1 ? decoded[1][i] : 0;
                if (assignment == 8)
                {
                    right = left - right;
                }
                else if (assignment == 9)
                {
                    left = left + right;
                }
                else if (assignment == 10)
                {
                    const int32_t sideValue = right;
                    const int32_t midValue = static_cast<int32_t>((static_cast<uint32_t>(left) << 1) | (static_cast<uint32_t>(sideValue) & 1));
                    left = (midValue + sideValue) >> 1;
                    right = (midValue - sideValue) >> 1;
                }
                int16_t* frame = samples + static_cast<size_t>(produced) * static_cast<size_t>(channels);
                frame[0] = static_cast<int16_t>(left);
                if (channels > 1)
                    frame[1] = static_cast<int16_t>(right);
                for (int c = 2; c < channels; c++)
                    frame[c] = static_cast<int16_t>(decoded[c][i]);
            }
        }
        return true;
    }

    /// endregion </Decoder>
}  // namespace chd::flac
