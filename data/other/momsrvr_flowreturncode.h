#ifndef INCLUDED_MOMSRVR_FLOWRETURNCODE
#define INCLUDED_MOMSRVR_FLOWRETURNCODE

namespace BloombergLP {
namespace momsrvr {

/**
 * Special return codes.
 *
 * @internal
 * These can be removed if there are existing constants.
 * @endinternal
 */
struct FlowReturnCode {
    /// The sentinel value indicating a function executed without any errors.
    static const FlowReturnCode Success;

    /// The sentinel value indicating a function failed to execute correctly.
    static const FlowReturnCode Error;

    /**
     * The value returned when there are no steps.
     *
     * Known to be returned from the following functions:
     * - momtktImpl::getFirmflow
     */
    static const FlowReturnCode NoStep;

    /**
     * The value returned when the end of a flow is reached.
     *
     * Known to be returned from the following functions:
     * - momtktImpl::getFirmflow
     * - momTktImpl::getFlowflags
     */
    static const FlowReturnCode FlowEnd;

    FlowReturnCode();

    bool operator==(const FlowReturnCode& other) const;

    bool operator!=(const FlowReturnCode& other) const;

    bool operator==(int value) const;

    bool operator!=(int value) const;

    /**
     * Gets the value of the return code.
     *
     * @return The value of the return code.
     */
    int getValue() const;

private:
    explicit FlowReturnCode(int value);

    int d_value;
};

} // namespace momsrvr
} // namespace BloombergLP

#endif
