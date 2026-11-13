#include "file/UploadReactor.h"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <syncstream>
#include <thread>

namespace {
using namespace std::chrono_literals;

std::string probeReactor(int port, std::uintmax_t file_size) {
    UniqueFd client{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!client.valid()) throw std::system_error(errno, std::generic_category(), "create probe socket");
    const timeval timeout{3, 0};
    if (::setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 ||
        ::setsockopt(client.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1) {
        throw std::system_error(errno, std::generic_category(), "set probe timeout");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::connect(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::system_error(errno, std::generic_category(), "connect to upload Reactor");
    }

    // 只发请求头，检查上传路由是否抵达当前授权入口。
    const std::string headers = "PUT /upload_file HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(file_size) + "\r\n\r\n";
    std::string_view request{headers};
    while (!request.empty()) {
        const auto sent = ::send(client.get(), request.data(), request.size(), MSG_NOSIGNAL);
        if (sent == -1 && errno == EINTR) continue;
        if (sent < 0) throw std::system_error(errno, std::generic_category(), "send probe");
        if (sent == 0) throw std::runtime_error("probe send made no progress");
        request.remove_prefix(static_cast<std::size_t>(sent));
    }
    std::string response;
    char buffer[1024];
    while (true) {
        const auto count = ::recv(client.get(), buffer, sizeof(buffer), 0);
        if (count == -1 && errno == EINTR) continue;
        if (count < 0) throw std::system_error(errno, std::generic_category(), "receive probe response");
        if (count == 0) return response;
        response.append(buffer, static_cast<std::size_t>(count));
        if (response.size() > 8192) throw std::runtime_error("probe response too large");
    }
}

std::filesystem::path makeTestPath() {
    const auto directory = std::filesystem::path{UPLOAD_TEST_PROJECT_ROOT} / "test" / "upload-reactor";
    std::filesystem::create_directories(directory);
    auto run_directory = (directory / "run-XXXXXX").string();
    if (::mkdtemp(run_directory.data()) == nullptr) throw std::system_error(errno, std::generic_category(), "create test run directory");
    return std::filesystem::path{run_directory} / "upload.part";
}

void testDiskPool(DiskPool& pool, const std::filesystem::path& source, const std::filesystem::path& path) {
    auto completion = std::make_shared<std::promise<void>>();
    auto finished = completion->get_future();
    const bool accepted = pool.receiveTask([completion, source, path] {
        try {
            std::osyncstream(std::cout) << "[disk] running, thread=" << std::this_thread::get_id() << std::endl;
            {
                std::ifstream incoming(source, std::ios::binary);
                if (!incoming) throw std::runtime_error("cannot read source JPG");
                DiskWriteTask task;
                task.temp_path = path.string();
                constexpr int open_flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
                std::osyncstream(std::cout) << "[disk] open(path=" << path
                    << ", flags=O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC [" << open_flags << "], mode=0600)" << std::endl;
                auto opened = DiskOperation::OpenTempFile(task);
                if (opened.error) throw std::system_error(opened.error, "create test upload");
                if (!opened.opened_file || !opened.opened_file->valid()) throw std::runtime_error("open returned no file fd");
                task.file = std::move(opened.opened_file);
                std::osyncstream(std::cout) << "[disk] opened file fd=" << task.file->get() << " path=" << path << std::endl;
                constexpr std::size_t chunk_size = 64 * 1024;
                for (std::size_t block = 1; ; ++block) {
                    // 本地 JPG 的每次读取模拟连接 socket 上收到的一块正文。
                    task.data.resize(chunk_size);
                    incoming.read(task.data.data(), static_cast<std::streamsize>(chunk_size));
                    const auto count = incoming.gcount();
                    if (incoming.bad() || (incoming.fail() && !incoming.eof())) throw std::runtime_error("source JPG read failed");
                    if (count == 0) break;
                    task.data.resize(static_cast<std::size_t>(count));
                    std::osyncstream(std::cout) << "[recv simulated] block=" << block << " bytes=" << count << std::endl;
                    std::osyncstream(std::cout) << "[disk] block=" << block << " pwrite request(file_fd=" << task.file->get()
                        << ", buf=" << static_cast<const void*>(task.data.data()) << ", count=" << task.data.size()
                        << ", offset=" << task.offset << ")" << std::endl;
                    const auto result = DiskOperation::writeBlock(task);
                    if (result.error) throw std::system_error(result.error, "write test upload");
                    if (result.offset != task.offset || result.written_bytes != task.data.size()) {
                        throw std::runtime_error("unexpected disk write offset or length");
                    }
                    std::osyncstream(std::cout) << "[disk] wrote offset=" << result.offset << " bytes=" << result.written_bytes << std::endl;
                    task.offset += result.written_bytes;
                }
            } // 先关闭临时文件 fd，再通知测试线程回读。
            completion->set_value();
        } catch (...) {
            completion->set_exception(std::current_exception());
        }
    });
    if (!accepted) throw std::runtime_error("disk pool rejected test task");
    std::osyncstream(std::cout) << "[disk] task accepted" << std::endl;
    if (finished.wait_for(5s) != std::future_status::ready) throw std::runtime_error("disk task timed out");
    finished.get();

    std::ifstream original(source, std::ios::binary);
    std::ifstream input(path, std::ios::binary);
    if (!original || !input) throw std::runtime_error("cannot open JPG files for comparison");
    const std::string expected{std::istreambuf_iterator<char>{original}, std::istreambuf_iterator<char>{}};
    const std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (content != expected) throw std::runtime_error("uploaded bytes do not match source JPG");
    std::osyncstream(std::cout) << "[disk] PASS readback matched " << content.size() << " bytes" << std::endl;
}
}

int main(int argc, char** argv) {
    try {
        int port = 18084;
        if (argc > 2) throw std::invalid_argument("usage: upload_reactor_disk_smoke_test [port]");
        if (argc == 2) {
            const std::string_view argument{argv[1]};
            const auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), port);
            if (error != std::errc{} || end != argument.data() + argument.size() || port < 1 || port > 65535) {
                throw std::invalid_argument("port must be between 1 and 65535");
            }
        }

        const auto source = std::filesystem::path{UPLOAD_TEST_PROJECT_ROOT} / "test" / "test.jpg";
        const auto file_size = std::filesystem::file_size(source);
        if (file_size == 0) throw std::runtime_error("source JPG is empty");
        std::cout << "[test] source=" << source << " bytes=" << file_size << std::endl;

        DiskPool pool{2, 8};
        DatabasePool database;
        FileRepository repository{database};
        FileService service{repository};
        AccessTokenService tokens{"upload-smoke-test"};
        AuthMiddleWare auth{tokens};
        UploadReactor reactor{port, pool, auth, service, [](auto) {
            throw std::logic_error("Unauthenticated probe reached the business queue");
        }};
        std::exception_ptr reactor_error;
        std::jthread reactor_thread([&](std::stop_token stop) {
            std::stop_callback stop_reactor(stop, [&] { reactor.close(); });
            try {
                std::osyncstream(std::cout) << "[reactor] run thread=" << std::this_thread::get_id() << std::endl;
                reactor.run();
                std::osyncstream(std::cout) << "[reactor] run returned" << std::endl;
            } catch (...) {
                reactor_error = std::current_exception();
            }
        });

        std::osyncstream(std::cout) << "[reactor] probing 127.0.0.1:" << port << " PUT /upload_file" << std::endl;
        const auto response = probeReactor(port, file_size);
        std::osyncstream(std::cout) << "[reactor] response: " << response << std::endl;
        if (!response.starts_with("HTTP/1.1 401 ") || response.find("invalid_access_token") == std::string::npos) {
            throw std::runtime_error("probe did not reject the missing access token");
        }
        std::osyncstream(std::cout) << "[reactor] PASS rejected missing access token" << std::endl;

        const auto path = makeTestPath();
        testDiskPool(pool, source, path);
        reactor_thread.request_stop();
        reactor_thread.join();
        if (reactor_error) std::rethrow_exception(reactor_error);
        pool.close();
        std::cout << "[test] PASS Reactor HTTP probe and independent disk write. Test file: " << path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[test] FAIL " << error.what() << '\n';
        return 1;
    }
}
