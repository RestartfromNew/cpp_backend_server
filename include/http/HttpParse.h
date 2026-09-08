//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_HTTPPARSE_H
#define CPP_BACKEND_SERVER_HTTPPARSE_H

#include "http/HttpRequest.h"
#include <string>
#include <utility>
#include <stdexcept>
/**
 * @brief 增量 HTTP 请求解析器。 (描述它代表什么)
 *
 * HttpParse 将调用者提供的字节流逐步解析为 HttpRequest。 （核心功能）
 * 一个完整请求可以被拆分到多次 parse() 调用中，Parser 会在
 * 调用之间保留已经解析出的请求字段和当前状态。
 *
 * 本类只负责解析内存中的字节： （负责什么和不负责什么，和设计时候原则保持一致）
 * - 不读取 socket；
 * - 不拥有 Connection；
 * - 不保存传入的 std::string_view；
 * - 不执行路由或业务处理。
 *
 * 典型调用流程：
 *
 * @code
 * （如何配合connection）
 * ParseResult result = parser.parse(connection.inputBuffer());
 * connection.consumeInput(result.consumed);
 *
 * if (result.status == ParseStatus::NeedMoreData) {
 *     // 从连接继续读取
 * } else if (result.status == ParseStatus::Complete) {
 *     HttpRequest request = parser.takeRequest();
 * } else {
 *     // 返回 HTTP 400
 * }
 * @endcode
 * （写明生命周期和线程）
 * 一个实例不能被多个线程同时调用。
 */

/**
 * @brief 一次增量解析调用的结果状态。
 */
enum class ParseStatus {
    /**
    * 当前字节不足以完成请求。(含义)
    *（怎么做）
    * 调用者应先移除 ParseResult::consumed 指定的前缀，
    * 然后从 Connection 读取更多数据并再次调用 parse()。
    */
    NeedMoreData,
    /**
     * 一个完整的 HTTP 请求已经解析完成。
     *
     * 调用者可以调用 takeRequest() 取得请求对象。
     */
    Complete,

    /**
     * 输入违反当前 Parser 支持的 HTTP 格式。
     *
     * 调用者通常应返回 HTTP 400，并关闭连接；
     * 若想复用 Parser，必须先调用 reset()。
     */
    Error
};

/**
 * @brief 一次调用parse()的结果
 */
struct ParseResult {

    ParseStatus status;

    /**
     * 本次调用已经确认解析完成的输入前缀长度。
     *
     * 调用者可以从 Connection 输入缓冲区中删除前 consumed 个字节。
     * 即使 status 为 NeedMoreData，consumed 也可能大于 0。
     *
     * 例如，请求行已经解析完成、但下一行 Header 尚不完整时，
     * consumed 包含完整请求行的长度，不包含未完成的 Header。
     */
    std::size_t consumed;
};

enum class StepStatus {
    Complete,
    NeedMoreData,
    Error
};

struct StepResult {
    StepStatus status;
    // 当前步骤可以安全移除的输入前缀长度。
    std::size_t consumed;
    // 仅用于 Header 步骤：是否读到了结束 Header 的空行。
    bool headersFinished = false;
};

class HttpParse {

    public:

    HttpParse()=default;
    ~HttpParse()=default;

    /**
     * @brief 尽可能解析当前可用的输入字节。
     *
     * Parser 会从当前内部状态继续解析，并在以下情况返回：
     * - 数据不足；
     * - 一个请求解析完成；
     * - 遇到格式错误。
     *
     * 本函数不会读取网络，也不会保存 buffer。
     *
     * @param buffer Connection 中尚未被消费的连续字节。（输入是什么，会做多久，什么时候返回）
     *
     * @return 当前解析状态和可以从输入缓冲区移除的前缀长度。
     *（返回值如何使用）
     *
     * @note 调用者必须在下一次 parse() 前移除返回的 consumed 字节；（下一步应该做什么）
     *       否则 Parser 会把已经处理的数据再次当成新输入。
     */
    ParseResult parse(std::string_view buffer);
    /** （转移了什么，转移后parser状态如何，在什么情况下调用，错误是什么）
     * @brief 取出已经完成解析的 HTTP 请求。
     *
     * 本函数把内部 HttpRequest 移动给调用者，并自动调用 reset()，
     * 因此成功返回后 Parser 可以开始解析下一个请求。
     *
     * @return 拥有自身字符串和 Header 数据的完整 HttpRequest。
     *
     * @throws std::logic_error 如果当前状态不是 Complete。
     */
    HttpRequest takeRequest();
        /**
     * @brief 丢弃当前解析进度并恢复初始状态。
     *
     * reset() 会清空已经解析出的请求字段和 Content-Length，
     * 下一次 parse() 将从请求行开始。
     *
     * 本函数不修改 Connection 的输入缓冲区。
     */
    void reset();
private:
    enum class State {
        RequestLine,
        Header,
        Body,
        Complete,
        Error
    };
    // 正在构建的请求；字段会跨多次 parse() 调用逐步填充。TCP 可能分包，所以请求需要跨调用逐步构建。
    HttpRequest request;
    // 下一次 parse() 应继续处理的协议部分。
    State state_=State::RequestLine;
    // 从 Content-Length 解析出的 Body 字节数。
    // Header 未提供 Content-Length 时保持 0。
    std::size_t content_length=0;
        /**
     * 解析一行：
     *
     * METHOD SP request-target SP HTTP-version CRLF
     *
     * 数据中没有完整 CRLF 时返回 NeedMoreData；
     * 语法错误或不支持的方法/版本返回 Error；
     * 成功时 consumed 包含结尾的 CRLF。
     */
    StepResult parseRequestLine(std::string_view input);
        /**
     * 解析一行 Header 或 Header 结束空行。
     *
     * 普通 Header 成功时：
     * - status 为 Complete；
     * - headersFinished 为 false。
     *
     * 遇到单独的 CRLF 时：
     * - status 为 Complete；
     * - headersFinished 为 true。
     *
     * consumed 始终包含本行结尾的 CRLF。
     */
    StepResult parseHeader(std::string_view input);
        /**
     * 根据 contentLength_ 提取请求 Body。
     *
     * 输入不足 contentLength_ 字节时返回 NeedMoreData；
     * 成功时只消费恰好 contentLength_ 字节，额外字节留给后续请求。
     */
    StepResult parseBody(std::string_view input);
    HttpMethod parseMethod(std::string_view method);
        /**
     * 严格解析 Content-Length。
     *
     * 只接受完整的非负十进制整数，并拒绝超过最大 Body 限制的值。
     * 成功时更新 contentLength_。
     *
     * @return 解析和范围检查都成功时返回 true。
     */
    bool parseContentLength(std::string_view value);



};


#endif //CPP_BACKEND_SERVER_HTTPPARSE_H
