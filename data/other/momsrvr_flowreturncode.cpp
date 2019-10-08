#ifndef INCLUDED_MOMSRVR_FLOWRETURNCODE
#include <momsrvr_flowreturncode.h>
#endif

namespace BloombergLP {
namespace momsrvr {

const FlowReturnCode FlowReturnCode::Success(0);
const FlowReturnCode FlowReturnCode::Error(1);
const FlowReturnCode FlowReturnCode::NoStep(2);
const FlowReturnCode FlowReturnCode::FlowEnd(99);

FlowReturnCode::FlowReturnCode()
    : d_value(1)
{
}

FlowReturnCode::FlowReturnCode(int value)
    : d_value(value)
{
    // Do nothing
}

bool FlowReturnCode::operator==(const FlowReturnCode& other) const
{
    return getValue() == other.getValue();
}

bool FlowReturnCode::operator!=(const FlowReturnCode& other) const
{
    return getValue() != other.getValue();
}

bool FlowReturnCode::operator==(int value) const
{
    return getValue() == value;
}

bool FlowReturnCode::operator!=(int value) const
{
    return getValue() != value;
}

int FlowReturnCode::getValue() const
{
    return d_value;
}

} // namespace momsrvr
} // namespace BloombergLP
