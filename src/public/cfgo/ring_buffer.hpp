#ifndef _CFGO_RING_BUFFER_HPP_
#define _CFGO_RING_BUFFER_HPP_

#include <vector>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <iterator>
#include <cstddef>

namespace cfgo
{
    template <typename T>
    struct AdaptiveRingBufferNode
    {
        std::vector<T> data;
        AdaptiveRingBufferNode *next = nullptr;

        AdaptiveRingBufferNode(std::size_t capacity) : data(capacity) {}
    };

    template <typename T>
    class AdaptiveRingBuffer
    {
    public:
        typedef AdaptiveRingBufferNode<T> Node;

        struct Iterator
        {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = T *;
            using reference = T &;

            Iterator(AdaptiveRingBuffer *parent) : m_parent(parent)
            {
                if (parent && !parent->empty())
                {
                    m_node = parent->m_outlet_node;
                    m_offset = parent->m_outlet_offset;
                }
                else
                {
                    parent = nullptr;
                }
            }

            reference operator*() const {
                return m_node->data[m_offset];
            }
            pointer operator->() {
                return &m_node->data[m_offset];
            }

            Iterator &operator++() {
                if (m_node)
                {
                    ++ m_offset;
                    if (m_offset == m_parent->m_segment_capacity)
                    {
                        m_node = m_node->next;
                        if (!m_node)
                        {
                            m_node = m_parent->m_head;
                        }
                        m_offset = 0;
                    }
                    if (m_node == m_parent->m_inlet_node && m_offset == m_parent->m_inlet_offset)
                    {
                        m_parent = nullptr;
                        m_node = nullptr;
                        m_offset = 0;
                    }
                }
                return *this;
            }

            // Postfix increment
            Iterator operator++(int)
            {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            friend bool operator==(const Iterator &a, const Iterator &b) {
                return a.m_parent == b.m_parent && a.m_node == b.m_node && a.m_offset == b.m_offset;
            };
            friend bool operator!=(const Iterator &a, const Iterator &b) {
                return a.m_parent != b.m_parent || a.m_node != b.m_node || a.m_offset != b.m_offset;
            };

        private:
            AdaptiveRingBuffer *m_parent;
            Node *m_node = nullptr;
            std::int32_t m_offset = 0;
        };

    private:
        std::int32_t m_segment_capacity;
        std::int32_t m_max_segments;
        std::int32_t m_min_segments;
        Node *m_head = nullptr;
        Node *m_inlet_node = nullptr;
        std::int32_t m_inlet_offset = 0;
        Node *m_outlet_node = nullptr;
        std::int32_t m_outlet_offset = 0;
        std::int32_t m_nodes = 0;

        void _init()
        {
            m_head = new Node(m_segment_capacity);
            m_inlet_node = m_outlet_node = m_head;
            // m_inlet_offset = m_outlet_offset = 0;
            m_nodes = 1;
        }

        void _step_outlet(std::int32_t step)
        {
            assert(step > 0);
            assert(m_head);
            auto new_offset = m_outlet_offset + step;
            auto node_offset = new_offset / m_segment_capacity;
            auto in_node_offset = new_offset % m_segment_capacity;
            auto node = m_outlet_node;
            auto prev = node == m_head ? nullptr : m_head;
            if (prev)
            {
                while (prev->next != node)
                {
                    prev = prev->next;
                    assert(prev);
                }
            }
            for (std::size_t i = 0; i < node_offset; ++i)
            {
                auto next = node->next;
                if (m_nodes > m_min_segments && node != m_inlet_node)
                {
                    if (prev)
                    {
                        prev->next = next;
                    }
                    else
                    {
                        assert(node == m_head);
                        assert(next);
                        m_head = next;
                    }
                    delete node;
                    --m_nodes;
                }
                if (!next)
                {
                    next = m_head;
                    prev = nullptr;
                }
                node = next;
            }
            m_outlet_node = node;
            m_outlet_offset = in_node_offset;
        }

        void _step_inlet(std::int32_t step)
        {
            assert(step >= 0);
            assert(m_head);
            auto new_offset = m_inlet_offset + step;
            auto node_offset = new_offset / m_segment_capacity;
            auto in_node_offset = new_offset % m_segment_capacity;
            auto node = m_inlet_node;
            for (std::size_t i = 0; i < node_offset; ++i)
            {
                if (!node->next)
                {
                    if (m_nodes < m_max_segments)
                    {
                        node = node->next = new Node(m_segment_capacity);
                        ++m_nodes;
                    }
                    else
                    {
                        node = m_head;
                    }
                }
                else
                {
                    node = node->next;
                }
            }
            m_inlet_node = node;
            m_inlet_offset = in_node_offset;
        }

    public:
        AdaptiveRingBuffer(std::int32_t segment_capacity, std::int32_t max_segments, std::int32_t min_segments) : m_segment_capacity(segment_capacity),
                                                                                                                  m_max_segments(max_segments),
                                                                                                                  m_min_segments(min_segments)
        {
            assert(segment_capacity > 1);
            assert(min_segments > 0);
            assert(max_segments >= min_segments);
        }
        ~AdaptiveRingBuffer()
        {
            auto node = m_head;
            while (node)
            {
                auto next = node->next;
                delete node;
                node = next;
            }
        }

        std::size_t size() const noexcept
        {
            if (!m_head)
            {
                return 0;
            }
            auto e = m_inlet_offset;
            auto n = m_outlet_node;
            while (n != m_inlet_node)
            {
                e += m_segment_capacity;
                n = n->next;
                if (!n)
                {
                    n = m_head;
                }
            }
            if (m_outlet_offset > e)
            {
                e += m_segment_capacity * m_nodes;
            }
            return e - m_outlet_offset;
        }

        std::size_t capacity() const noexcept
        {
            return m_nodes > 0 ? m_nodes * m_segment_capacity - 1 : 0;
        }

        bool empty() const noexcept
        {
            return !m_head || (m_inlet_node == m_outlet_node && m_inlet_offset == m_outlet_offset);
        }

        bool full() const noexcept
        {
            return m_head && ((m_inlet_node == m_outlet_node && m_inlet_offset + 1 == m_outlet_offset) || ((m_inlet_node->next == m_outlet_node || (m_nodes == m_max_segments && m_inlet_node->next == nullptr && m_outlet_node == m_head)) && m_outlet_offset == 0 && m_inlet_offset + 1 == m_segment_capacity));
        }

        bool enqueue(T &&value, bool force)
        {
            if (!force && full())
            {
                return false;
            }
            if (!m_head)
            {
                _init();
            }
            bool is_full = full();
            m_inlet_node->data[m_inlet_offset] = std::forward<T>(value);
            _step_inlet(1);
            if (is_full)
            {
                _step_outlet(1);
            }
            return true;
        }

        bool dequeue(T &out)
        {
            if (empty())
            {
                return false;
            }
            out = std::move(m_outlet_node->data[m_outlet_offset]);
            _step_outlet(1);
            return true;
        }

        bool dequeue()
        {
            if (empty())
            {
                return false;
            }
            _step_outlet(1);
            return true;
        }

        const T *queue_head() const noexcept
        {
            if (empty())
            {
                return nullptr;
            }
            else
            {
                return &m_outlet_node->data[m_outlet_offset];
            }
        }

        T *queue_head() noexcept
        {
            if (empty())
            {
                return nullptr;
            }
            else
            {
                return &m_outlet_node->data[m_outlet_offset];
            }
        }

        Iterator begin() {
            return Iterator(this);
        }

        Iterator end() {
            return Iterator(nullptr);
        }

        template <typename CB>
            requires requires(CB cb, T &v) {
                requires(
                    requires { { cb(v) } -> std::same_as<bool>; } ||
                    requires { { cb(v) } -> std::same_as<void>; });
            }
        void foreach (CB &&cb)
        {
            if (empty())
            {
                return;
            }
            Node *node = m_outlet_node;
            auto offset = m_outlet_offset;
            do
            {
                auto &v = node->data[offset++];
                if constexpr (std::is_same_v<decltype(cb(v)), bool>)
                {
                    if (!cb(v))
                    {
                        break;
                    }
                }
                else
                {
                    cb(v);
                }
                if (offset == m_segment_capacity)
                {
                    node = node->next;
                    if (!node)
                    {
                        node = m_head;
                    }
                    offset = 0;
                }
                if (node == m_inlet_node && offset == m_inlet_offset)
                {
                    break;
                }
            } while (true);
        }
    };

} // namespace cfgo

#endif
