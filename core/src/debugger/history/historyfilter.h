#pragma once
/// @file historyfilter.h
/// @brief The record-time filter of a device history (devicehistory.h): which records are kept and streamed at all.
/// A history in capture mode with a stream keeps every record, and most of a busy device's records are not the ones
/// a study needs (a GS card: the firmware's idle polls and its 37.5 kHz interrupts beside a handful of host writes).
/// The filter drops the others where they are pushed, so neither the memory nor the file sees them.
///
/// Text form, the same on every automation plane:
///
///     side=host out=true            every term must hold
///     port=#B3,#BB pc=#8000-#9FFF   a term holds when one of its values does; a-b is a range (inclusive)
///
/// A term is `field=value[,value...]`, terms are separated by spaces. A field is a name of the record as the history
/// shows it (its ToState: `side`, `port`, `pc`, `value`, `dir`, ...). A value is a number (decimal; hex as #B3, $B3
/// or 0xB3), a range of numbers, or a word (`host`, `out`, `true`). A record without the field does not pass.
/// Numbers compare with number fields (and 0 / 1 with flags), words with text fields without case and with flags
/// (true / false, yes / no, on / off).
///
/// Two ways to test a record, with the same result:
///   - any record: its ToState rendered and tested (RecordFilter::Matches); fine for a device that records commands;
///   - a record that names its fields (`static FieldGetter<R> FilterField(name)`, `static bool FilterWord(field,
///     word, value)`, `static const char* FilterFields()`): the filter is compiled once into getters and ranges, a
///     test is a few compares. The busy histories (GS, ports, execution) do this.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

namespace history
{

struct FilterRange
{
    int64_t lo = 0;
    int64_t hi = 0;
};

struct FilterTerm
{
    std::string field;
    std::vector<FilterRange> numbers;
    std::vector<std::string> words;     ///< lower case
};

/// A record's field as a number; false: this record has no such field
template <typename Record>
using FieldGetter = bool (*)(const Record& record, int64_t& value);

class RecordFilter
{
public:
    /// "" is no filter (Empty). False with `error` for a term that is not `field=value[,value...]`
    static bool Parse(const std::string& text, RecordFilter& filter, std::string& error);

    bool Empty() const { return _terms.empty(); }
    const std::vector<FilterTerm>& Terms() const { return _terms; }
    /// The text it was parsed from, terms separated by one space
    const std::string& Text() const { return _text; }

    /// A record as its ToState object
    bool Matches(const StateNode& record) const;

    /// true / false, yes / no, on / off (lower case)
    static bool FlagWord(const std::string& word, bool& value);

private:
    std::vector<FilterTerm> _terms;
    std::string _text;
};

}  // namespace history
