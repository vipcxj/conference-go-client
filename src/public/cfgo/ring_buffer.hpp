#ifndef _CFGO_RING_BUFFER_HPP_
#define _CFGO_RING_BUFFER_HPP_

#include <vector>
#include <cstdint>
#include <cassert>

namespace cfgo
{
    template<typename T>
    struct AdaptiveRingBufferNode {
        std::vector<T> data;
        AdaptiveRingBufferNode * next = nullptr;

        AdaptiveRingBufferNode(std::size_t capacity): data(capacity) {}
    };

    template<typename T>
    class AdaptiveRingBuffer
    {
        typedef AdaptiveRingBufferNode<T> Node;
    private:
        std::int32_t m_segment_capacity;
        std::int32_t m_max_segments;
        std::int32_t m_min_segments;
        Node * m_head = nullptr;
        Node * m_inlet_node = nullptr;
        std::int32_t m_inlet_offset = 0;
        Node * m_outlet_node = nullptr;
        std::int32_t m_outlet_offset = 0;
        std::int32_t m_nodes = 0;

        void _init() {
            m_head = new Node(m_segment_capacity);
            m_inlet_node = m_outlet_node = m_head;
            // m_inlet_offset = m_outlet_offset = 0;
            m_nodes = 1;
        }

        void _step_outlet(std::int32_t step) {
            assert(step >= 0);
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
                assert(node != m_inlet_node || m_max_segments == 1);
                auto next = node->next;
                if (m_nodes > m_min_segments)
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

        void _step_inlet(std::int32_t step) {
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
                else {
                    node = node->next;
                }
            }
            m_inlet_node = node;
            m_inlet_offset = in_node_offset;
        }
    public:
        AdaptiveRingBuffer(std::int32_t segment_capacity, std::int32_t max_segments, std::int32_t min_segments):
            m_segment_capacity(segment_capacity),
            m_max_segments(max_segments),
            m_min_segments(min_segments)
        {
            assert(segment_capacity > 1);
            assert(min_segments > 0);
            assert(max_segments >= min_segments);
        }
        ~AdaptiveRingBuffer
        () {
            auto node = m_head;
            while (node)
            {
                auto next = node->next;
                delete node;
                node = next;
            }
        }

        std::size_t size() const noexcept {
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
            }
            if (m_outlet_offset > e)
            {
                e += m_segment_capacity * m_nodes;
            }
            return e - m_outlet_offset;
        }

        bool empty() const noexcept {
            return !m_head || (m_inlet_node == m_outlet_node && m_inlet_offset == m_outlet_offset);
        }

        bool full() const noexcept {
            return m_head 
                && (
                    (m_inlet_node == m_outlet_node && m_inlet_offset + 1 == m_outlet_offset)
                    || ((m_inlet_node->next == m_outlet_node || (m_nodes == m_max_segments && m_inlet_node->next == nullptr && m_outlet_node == m_head)) && m_outlet_offset == 0 && m_inlet_offset + 1 == m_segment_capacity)
                );
        }

        bool enqueue(T && value, bool force) {
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

        bool dequeue(T & out) {
            if (empty())
            {
                return false;
            }
            out = std::move(m_outlet_node->data[m_outlet_offset]);
            _step_outlet(1);
            return true;
        }

        const T * queue_head() const noexcept {
            if (empty())
            {
                return nullptr;
            }
            else
            {
                return &m_outlet_node->data[m_outlet_offset];
            }
        }
    };
    
} // namespace cfgo


#endif
