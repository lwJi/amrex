#ifndef AMREX_IPARSER_H_
#define AMREX_IPARSER_H_

#include <AMReX_Arena.H>
#include <AMReX_Array.H>
#include <AMReX_GpuDevice.H>
#include <AMReX_IParser_Exe.H>
#include <AMReX_Vector.H>

#include <memory>
#include <string>
#include <set>

namespace amrex {

template <int N>
struct IParserExecutor
{
    template <int M=N, std::enable_if_t<M==0,int> = 0>
    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    long long operator() () const noexcept
    {
        AMREX_IF_ON_DEVICE((return iparser_exe_eval(m_device_executor, nullptr);))
        AMREX_IF_ON_HOST((return iparser_exe_eval(m_host_executor, nullptr);))
    }

    template <typename... Ts>
    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    std::enable_if_t<sizeof...(Ts) == N && std::conjunction_v<std::is_integral<Ts>...>,
                     long long>
    operator() (Ts... var) const noexcept
    {
        amrex::GpuArray<long long,N> l_var{var...};
        AMREX_IF_ON_DEVICE((return iparser_exe_eval(m_device_executor, l_var.data());))
        AMREX_IF_ON_HOST((return iparser_exe_eval(m_host_executor, l_var.data());))
    }

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    long long operator() (GpuArray<long long, N> const& var) const noexcept
    {
        AMREX_IF_ON_DEVICE((return iparser_exe_eval(m_device_executor, var.data());))
        AMREX_IF_ON_HOST((return iparser_exe_eval(m_host_executor, var.data());))
    }

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    explicit operator bool () const {
        AMREX_IF_ON_DEVICE((return m_device_executor != nullptr;))
        AMREX_IF_ON_HOST((return m_host_executor != nullptr;))
    }

    char* m_host_executor = nullptr;
#ifdef AMREX_USE_GPU
    char* m_device_executor = nullptr;
#endif
};

class IParser
{
public:
    IParser (std::string const& func_body);
    IParser () = default;
    void define (std::string const& func_body);

    explicit operator bool () const;

    void setConstant (std::string const& name, long long c);

    void registerVariables (Vector<std::string> const& vars);

    void print () const;

    [[nodiscard]] int depth () const;
    [[nodiscard]] int maxStackSize () const;

    [[nodiscard]] std::string expr () const;

    [[nodiscard]] std::set<std::string> symbols () const;

    //! This compiles for both GPU and CPU
    template <int N> [[nodiscard]] IParserExecutor<N> compile () const;

    //! This compiles for CPU only
    template <int N> [[nodiscard]] IParserExecutor<N> compileHost () const;

private:

    struct Data {
        std::string m_expression;
        struct amrex_iparser* m_iparser = nullptr;
        int m_nvars = 0;
        bool m_use_arena = true;
        char* m_host_executor = nullptr;
#ifdef AMREX_USE_GPU
        char* m_device_executor = nullptr;
#endif
        int m_max_stack_size = 0;
        int m_exe_size = 0;
        Data () = default;
        ~Data ();
        Data (Data const&) = delete;
        Data (Data &&) = delete;
        Data& operator= (Data const&) = delete;
        Data& operator= (Data &&) = delete;
    };

    std::shared_ptr<Data> m_data;
};

template <int N>
IParserExecutor<N>
IParser::compileHost () const
{
    if (m_data && m_data->m_iparser) {
        AMREX_ASSERT(N == m_data->m_nvars);

        if (!(m_data->m_host_executor)) {
            int stack_size;
            m_data->m_exe_size = static_cast<int>
                (iparser_exe_size(m_data->m_iparser, m_data->m_max_stack_size,
                                  stack_size));

            if (m_data->m_max_stack_size > AMREX_IPARSER_STACK_SIZE) {
                amrex::Abort("amrex::IParser: AMREX_IPARSER_STACK_SIZE, "
                             + std::to_string(AMREX_IPARSER_STACK_SIZE) + ", is too small for "
                             + m_data->m_expression);
            }
            if (stack_size != 0) {
                amrex::Abort("amrex::IParser: something went wrong with iparser stack! "
                             + std::to_string(stack_size));
            }

            m_data->m_host_executor = (char*)The_Pinned_Arena()->alloc(m_data->m_exe_size);
            if (m_data->m_host_executor == nullptr) { // Arena is not ready yet
                m_data->m_host_executor = (char*) std::malloc(m_data->m_exe_size);
                m_data->m_use_arena = false;
            }

            try {
                iparser_compile(m_data->m_iparser, m_data->m_host_executor);
            } catch (const std::runtime_error& e) {
                throw std::runtime_error(std::string(e.what()) + " in IParser expression \""
                                         + m_data->m_expression + "\"");
            }
        }

#ifdef AMREX_USE_GPU
        return IParserExecutor<N>{m_data->m_host_executor, m_data->m_device_executor};
#else
        return IParserExecutor<N>{m_data->m_host_executor};
#endif
    } else {
        return IParserExecutor<N>{};
    }
}

template <int N>
IParserExecutor<N>
IParser::compile () const
{
    auto exe = compileHost<N>();

#ifdef AMREX_USE_GPU
    if (m_data && m_data->m_iparser && !(m_data->m_device_executor)
        && m_data->m_use_arena)
    {
        m_data->m_device_executor = (char*)The_Arena()->alloc(m_data->m_exe_size);
        Gpu::htod_memcpy_async(m_data->m_device_executor, m_data->m_host_executor,
                               m_data->m_exe_size);
        Gpu::streamSynchronize();
        exe.m_device_executor = m_data->m_device_executor;
    }
#endif

    return exe;
}

}

#endif
