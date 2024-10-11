#ifndef _CFGO_SMART_LIST_HPP_
#define _CFGO_SMART_LIST_HPP_

#include <utility>
#include <memory>

#include "cfgo/alias.hpp"

namespace cfgo
{
    template<typename T>
    class smart_list;

    template<typename T>
    class smart_node {
    public:
        using ptr = std::shared_ptr<smart_node<T>>;
        using wptr = std::weak_ptr<smart_node<T>>;

        smart_node(T && data): m_data(std::forward<T>(data)) {}
        smart_node(const smart_node &) = delete;
        smart_node(smart_node &&) = delete;
        smart_node & operator = (const smart_node &) = delete;
        smart_node & operator = (smart_node &&) = delete;
        const T & value() const
        {
            return m_data;
        }
        auto read() const
        {
            return m_data.read();
        }
    private:
        T m_data;
        ptr m_next {};
        wptr m_prev {};
        friend class smart_list<T>;
    };

    template<typename T>
    class smart_list
    {
    public:
        using node_t = smart_node<T>;

        node_t::ptr add(T && data)
        {
            auto node = std::make_shared<node_t>(std::forward<T>(data));
            std::lock_guard lk(m_mux);
            if (m_head)
            {
                m_head->m_prev = node;
                node->m_next = m_head;
            }
            m_head = node;
            return node;
        }
        void remove(node_t::ptr & ptr)
        {
            std::lock_guard lk(m_mux);
            if (ptr == m_head)
            {
                m_head = ptr->m_next;
            }
            if (auto prev = ptr->m_prev.lock())
            {
                prev->m_next = ptr->m_next;
            }
            if (auto next = ptr->m_next)
            {
                next->m_prev = ptr->m_prev;
            }
            ptr->m_next.reset();
            ptr->m_prev.reset();
        }
        void remove(node_t::wptr & weak_ptr)
        {
            if (auto ptr = weak_ptr.lock())
            {
                remove(ptr);
            }
        }
        template<typename F>
        requires requires(F f, const T & e) {
            f(e);
        }
        void consume_all(F f)
        {
            std::lock_guard lk(m_mux);
            while (m_head)
            {
                auto node = m_head;
                f(node->value());
                m_head = node->m_next;
                if (m_head)
                {
                    m_head->m_prev.reset();
                }
                node->m_next.reset();
            }
        }

        private:
            node_t::ptr m_head;
            mutex m_mux;

    };

    template<typename T>
    class unique_smart_node
    {
    public:
        using list_t = smart_list<T>;
        using node_t = list_t::node_t;
        using node_ptr_t = node_t::ptr;

        unique_smart_node(): m_list(nullptr), m_node(nullptr), m_own(false) {}
        unique_smart_node(list_t * list, node_ptr_t && node): m_list(list), m_node(std::forward<node_ptr_t>(node)) {}
        unique_smart_node(list_t & list, node_ptr_t && node): m_list(&list), m_node(std::forward<node_ptr_t>(node)) {}
        unique_smart_node(unique_smart_node && other): m_list(other.m_list), m_node(std::move(other.m_node)), m_own(other.m_own)
        {
            other.m_own = false;
        }
        unique_smart_node(const unique_smart_node &) = delete;
        unique_smart_node & operator= (unique_smart_node && other)
        {
            m_list = other.m_list;
            m_node = std::move(other.m_node);
            m_own = other.m_own;
            other.m_own = false;
            return *this;
        }
        unique_smart_node & operator= (const unique_smart_node &) = delete;
        ~unique_smart_node()
        {
            if (m_own)
            {
                assert(m_list);
                m_list->remove(m_node);
            }
        }
        const T & operator* () const
        {
            return m_node->value();
        }
        const T * operator-> () const
        {
            return &m_node->value();
        }
        operator bool() const noexcept
        {
            return m_own && m_node;
        }
    private:
        list_t * m_list;
        node_ptr_t m_node;
        bool m_own = true;
    }; 
} // namespace cfgo


#endif