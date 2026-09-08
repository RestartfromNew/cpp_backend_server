//
// Created by yangb on 2026/9/7.
//

#ifndef CPP_BACKEND_SERVER_UNIQUEFD_H
#define CPP_BACKEND_SERVER_UNIQUEFD_H
/**
 * @brief Represents a single client TCP connection.
 *
 * Connection owns the client socket and manages the input and output
 * buffers associated with that connection.
 *
 * The underlying file descriptor is automatically released when the
 * Connection object is destroyed.
 */

class UniqueFd {
public:
    UniqueFd()noexcept;
    explicit UniqueFd(int fd)noexcept;
    ~UniqueFd()noexcept;
    //fd不允许共享，只能独占
    /**
    * @brief Constructs a Connection and takes ownership of the socket.
    *
    * @param fd Client socket file descriptor.
    */
    UniqueFd(const UniqueFd& other)=delete;
    UniqueFd& operator=(const UniqueFd& other)=delete;
    //fd的所有权可以共享
    UniqueFd(UniqueFd&& other)noexcept;
    UniqueFd& operator=(UniqueFd&& other)noexcept;
    /**
     *@brief Lend the file descriptor to the caller
     */
    [[nodiscard]]int get() const noexcept;
    [[nodiscard]]bool valid()const noexcept;
    /**
     * @brief Close the resources of the fd and set it -1
     */
    int release() noexcept;
    void reset(int new_fd=-1) noexcept;
private:
    int fd_=-1;
};


#endif //CPP_BACKEND_SERVER_UNIQUEFD_H
