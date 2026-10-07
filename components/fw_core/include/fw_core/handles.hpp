#pragma once

/// @file handles.hpp
/// @brief Narrow handles: each exposes one end of one channel (CS-OWN-06).
/// @details A producer's Config holds a Writer or a Sender; a consumer's holds a Reader or a
///          Receiver. Each channel header specialises the handles it supports:
///          | Channel        | write end         | read end            |
///          | Mailbox<T>     | Writer            | Reader              |
///          | AtomicValue<T> | Writer            | Reader              |
///          | Queue<T, ...>  | Sender            | Receiver            |
///          A handle is a small copyable value that points at the channel; the channel must
///          outlive it (the Topology owns every channel for the life of the firmware).

namespace hmi::fw {

/// @brief The write end of a state channel (Mailbox or AtomicValue).
template <class Channel> class Writer;

/// @brief The read end of a state channel (Mailbox or AtomicValue).
template <class Channel> class Reader;

/// @brief The send end of a Queue.
template <class Channel> class Sender;

/// @brief The receive end of a Queue.
template <class Channel> class Receiver;

} // namespace hmi::fw
