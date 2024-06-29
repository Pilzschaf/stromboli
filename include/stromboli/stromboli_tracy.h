#ifndef STROMBOLI_TRACY_H
#define STROMBOLI_TRACY_H

#include <stromboli/stromboli.h>

#if !defined TRACY_ENABLE

#define TracyCVkContext(x,y,z,w,a) (TracyStromboliContext){0}
#define TracyCVkContextCalibrated(x,y,z,w,a,b,c) (TracyStromboliContext){0}
#define TracyCVkDestroy(x)
#define TracyCVkContextName(c,x,y)
#define TracyCVkNamedZone(cn,c,x,y,z,w)
#define TracyCVkNamedZoneC(cn,c,x,y,z,w,a)
#define TracyCVkZone(cn,c,x,y)
#define TracyCVkZoneC(cn,c,x,y,z)
#define TracyCVkZoneTransient(cn, c,x,y,z,w)
#define TracyCVkCollect(c,x)
#define TracyCVkZoneEnd( ctx_name ) destroyTracyStromboliScope( ctx_name );

#define TracyCVkNamedZoneS(cn,c,x,y,z,w,a)
#define TracyCVkNamedZoneCS(cn,c,x,y,z,w,v,a)
#define TracyCVkZoneS(cn,c,x,y,z)
#define TracyCVkZoneCS(cn,c,x,y,z,w)
#define TracyCVkZoneTransientS(cn, c,x,y,z,w,a)

typedef struct { u8 dummy; } TracyStromboliContext;

#define TRACY_ZONE_HELPER(name)
#define TRACY_VK_ZONE_HELPER(ctx, cmdbuf, name)

#define initStromboliTracyContext(context)
#define destroyStromboliTracyContext(context)
#define collectTracyStromboli(commandBuffer)
#define getTracyContext();

#else // TRACY_ENABLE

#include <tracy/TracyC.h>
#include <time.h>
#include <stdlib.h> // For malloc

typedef struct TracyStromboliContext {
    VkDevice device;
    VkQueryPool query;
    VkTimeDomainEXT timeDomain;
    u64 deviation;
    s64 qpcToNs;
    s64 prevCalibration;
    u8 context;

    u32 head;
    u32 tail;
    u32 oldCnt;
    u32 queryCount;

    s64* res;

    PFN_vkGetCalibratedTimestampsEXT functionVkGetCalibratedTimestampsEXT;
} TracyStromboliContext;

typedef struct TracyStromboliScope {
    bool active;

    VkCommandBuffer commandBuffer;
    TracyStromboliContext* context;
} TracyStromboliScope;

struct SourceLocationData {
    const char* name;
    const char* function;
    const char* file;
    uint32_t line;
    uint32_t color;
};

static void calibrate(TracyStromboliContext* context, VkDevice device, int64_t* tCpu, int64_t* tGpu);
static u32 nextQueryId(TracyStromboliContext* context);
static TracyStromboliContext createTracyStromboliContext(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VkCommandBuffer cmdbuf, VkCommandPool commandPool, PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT _vkGetPhysicalDeviceCalibrateableTimeDomainsEXT, PFN_vkGetCalibratedTimestampsEXT _vkGetCalibratedTimestampsEXT);
static void destroyTracyStromboliContext(TracyStromboliContext* context);
static void tracyStromboliCollect(TracyStromboliContext* context, VkCommandBuffer commandBuffer);
static TracyStromboliScope createTracyStromboliScope( TracyStromboliContext* context, struct SourceLocationData* srcloc, VkCommandBuffer cmdbuf, bool is_active);
static TracyStromboliScope createTracyStromboliScopeWithCallstack( TracyStromboliContext* context, struct SourceLocationData* srcloc, VkCommandBuffer cmdbuf, int depth, bool is_active);
static TracyStromboliScope createTracyStromboliScopeAllocSource( TracyStromboliContext* context, uint32_t line, const char* source, size_t sourceSz, const char* function, size_t functionSz, const char* name, size_t nameSz, VkCommandBuffer cmdbuf, bool is_active);
static void destroyTracyStromboliScope(TracyStromboliScope* scope);

static inline TracyStromboliContext createTracyStromboliContext(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VkCommandBuffer cmdbuf, VkCommandPool commandPool, PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT _vkGetPhysicalDeviceCalibrateableTimeDomainsEXT, PFN_vkGetCalibratedTimestampsEXT _vkGetCalibratedTimestampsEXT) {
    TracyStromboliContext result = {
        .device = device,
        .timeDomain = VK_TIME_DOMAIN_DEVICE_EXT,
        .context = 0, //TODO: More contexts
        //.context = getGpuCtxCounter().fetch_add(1, std::memory_order_relayer),
        .head = 0,
        .tail = 0,
        .oldCnt = 0,
        .queryCount = 64*1024,
        .functionVkGetCalibratedTimestampsEXT = _vkGetCalibratedTimestampsEXT,
    };
    
    ASSERT(result.context != 255);
    
    if( _vkGetPhysicalDeviceCalibrateableTimeDomainsEXT && _vkGetCalibratedTimestampsEXT ) {
        uint32_t num;
        _vkGetPhysicalDeviceCalibrateableTimeDomainsEXT( physicalDevice, &num, 0);
        if( num > 4 ) num = 4;
        VkTimeDomainEXT data[4];
        _vkGetPhysicalDeviceCalibrateableTimeDomainsEXT( physicalDevice, &num, data );
        VkTimeDomainEXT supportedDomain = (VkTimeDomainEXT)-1;
#if defined _WIN32
        supportedDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
#elif defined __linux__ && defined CLOCK_MONOTONIC_RAW
        supportedDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT;
#endif
        for( uint32_t i=0; i<num; i++ )
        {
            if( data[i] == supportedDomain )
            {
                result.timeDomain = data[i];
                break;
            }
        }
    }

    VkPhysicalDeviceProperties prop;
    vkGetPhysicalDeviceProperties( physicalDevice, &prop );
    const float period = prop.limits.timestampPeriod;

    VkQueryPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryCount = result.queryCount;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    while( vkCreateQueryPool( device, &poolInfo, 0, &result.query ) != VK_SUCCESS )
    {
        result.queryCount /= 2;
        poolInfo.queryCount = result.queryCount;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdbuf;

    vkBeginCommandBuffer( cmdbuf, &beginInfo );
    vkCmdResetQueryPool( cmdbuf, result.query, 0, result.queryCount );
    vkEndCommandBuffer( cmdbuf );
    vkQueueSubmit( queue, 1, &submitInfo, VK_NULL_HANDLE );
    vkQueueWaitIdle( queue );
    vkResetCommandPool(device, commandPool, 0);

    int64_t tcpu, tgpu;
    if( result.timeDomain == VK_TIME_DOMAIN_DEVICE_EXT )
    {
        vkBeginCommandBuffer( cmdbuf, &beginInfo );
        vkCmdWriteTimestamp( cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, result.query, 0 );
        vkEndCommandBuffer( cmdbuf );
        vkQueueSubmit( queue, 1, &submitInfo, VK_NULL_HANDLE );
        vkQueueWaitIdle( queue );
        vkResetCommandPool(device, commandPool, 0);

        vkGetQueryPoolResults( device, result.query, 0, 1, sizeof( tgpu ), &tgpu, sizeof( tgpu ), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );

        vkBeginCommandBuffer( cmdbuf, &beginInfo );
        vkCmdResetQueryPool( cmdbuf, result.query, 0, 1 );
        vkEndCommandBuffer( cmdbuf );
        vkQueueSubmit( queue, 1, &submitInfo, VK_NULL_HANDLE );
        vkQueueWaitIdle( queue );
    }
    else
    {
        enum { NumProbes = 32 };

        VkCalibratedTimestampInfoEXT spec[2] = {
            { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, 0, VK_TIME_DOMAIN_DEVICE_EXT },
            { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, 0, result.timeDomain },
        };
        uint64_t ts[2];
        uint64_t deviation[NumProbes];
        for( int i=0; i<NumProbes; i++ )
        {
            _vkGetCalibratedTimestampsEXT( device, 2, spec, ts, deviation+i );
        }
        uint64_t minDeviation = deviation[0];
        for( int i=1; i<NumProbes; i++ )
        {
            if( minDeviation > deviation[i] )
            {
                minDeviation = deviation[i];
            }
        }
        result.deviation = minDeviation * 3 / 2;

#if defined _WIN32
        m_qpcToNs = int64_t( 1000000000. / GetFrequencyQpc() );
#endif

        calibrate(&result, device, &result.prevCalibration, &tgpu );
        //tcpu = Profiler::GetTime();
    }

    uint8_t flags = 0;
    if( result.timeDomain != VK_TIME_DOMAIN_DEVICE_EXT ) flags |= 1; // GpuContextCalibration

    struct ___tracy_gpu_new_context_data params = {
        .context = result.context,
        .gpuTime = tgpu,
        .period = period,
        .flags = flags,
        .type = 2, // Gpu context type vulkan. See TracyQueue.hpp:383
    };
    ___tracy_emit_gpu_new_context_serial(params);

/*
#ifdef TRACY_ON_DEMAND
    GetProfiler().DeferItem( *item );
#endif
    Profiler::QueueSerialFinish();*/

    //result.res = (int64_t*)tracy_malloc( sizeof( int64_t ) * result.queryCount );
    result.res = (int64_t*)malloc( sizeof( int64_t ) * result.queryCount );
    return result;
}

static inline void destroyTracyStromboliContext(TracyStromboliContext* context) {
    //tracy_free( context->res );
    free(context->res);
    vkDestroyQueryPool( context->device, context->query, 0);
}

static inline void tracyStromboliName(TracyStromboliContext* context, const char* name, u16 len) {
    //auto ptr = (char*)tracy_malloc( len );
    char* ptr = (char*)malloc(len);
    memcpy( ptr, name, len );

    struct ___tracy_gpu_context_name_data params = {
        .context = context->context,
        .name = ptr,
        .len = len,
    };
    ___tracy_emit_gpu_context_name_serial(params);
}

static inline void tracyStromboliCollect(TracyStromboliContext* context, VkCommandBuffer commandBuffer) {
    //TracyCZoneNC(tracyStromboliCollect, "tracyStromboliCollect", 0x8b0000, true);

    if( context->tail == context->head ) {
        //TracyCZoneEnd(tracyStromboliCollect);
        return;
    }

#ifdef TRACY_ON_DEMAND
    if( !GetProfiler().IsConnected() )
    {
        vkCmdResetQueryPool( cmdbuf, m_query, 0, m_queryCount );
        m_head = m_tail = m_oldCnt = 0;
        int64_t tgpu;
        if( m_timeDomain != VK_TIME_DOMAIN_DEVICE_EXT ) Calibrate( m_device, m_prevCalibration, tgpu );
        //TracyCZoneEnd(tracyStromboliCollect)
        return;
    }
#endif

    unsigned int cnt;
    if( context->oldCnt != 0 )
    {
        cnt = context->oldCnt;
        context->oldCnt = 0;
    }
    else
    {
        cnt = context->head < context->tail ? context->queryCount - context->tail : context->head - context->tail;
    }

    if( vkGetQueryPoolResults( context->device, context->query, context->tail, cnt, sizeof( int64_t ) * context->queryCount, context->res, sizeof( int64_t ), VK_QUERY_RESULT_64_BIT ) == VK_NOT_READY )
    {
        context->oldCnt = cnt;
        //TracyCZoneEnd(tracyStromboliCollect);
        return;
    }

    for( unsigned int idx=0; idx<cnt; idx++ ) {
        struct ___tracy_gpu_time_data params = {
            .context = context->context,
            .gpuTime = context->res[idx],
            .queryId = context->tail + idx,
        };
        ___tracy_emit_gpu_time_serial(params);
    }

    if( context->timeDomain != VK_TIME_DOMAIN_DEVICE_EXT )
    {
        int64_t tgpu, tcpu;
        calibrate(context, context->device, &tcpu, &tgpu );
        const int64_t delta = tcpu - context->prevCalibration;
        if( delta > 0 )
        {
            context->prevCalibration = tcpu;
            struct ___tracy_gpu_calibration_data params = {
                .context = context->context,
                .gpuTime = tgpu,
                .cpuDelta = delta,
            };
            ___tracy_emit_gpu_calibration_serial(params);
        }
    }

    vkCmdResetQueryPool( commandBuffer, context->query, context->tail, cnt );

    context->tail += cnt;
    if( context->tail == context->queryCount ) context->tail = 0;

    //TracyCZoneEnd(tracyStromboliCollect);
}

static inline u32 nextQueryId(TracyStromboliContext* context) {
    u32 id = context->head;
    context->head = ( context->head + 1 ) % context->queryCount;
    assert( context->head != context->tail );
    return id;
}

static inline uint8_t getId(TracyStromboliContext* context) {
    return context->context;
}

static inline void calibrate(TracyStromboliContext* context, VkDevice device, int64_t* tCpu, int64_t* tGpu ) {
    assert( context->timeDomain != VK_TIME_DOMAIN_DEVICE_EXT );
    VkCalibratedTimestampInfoEXT spec[2] = {
        { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, 0, VK_TIME_DOMAIN_DEVICE_EXT },
        { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, 0, context->timeDomain },
    };
    uint64_t ts[2];
    uint64_t deviation;
    do
    {
        context->functionVkGetCalibratedTimestampsEXT( device, 2, spec, ts, &deviation );
    }
    while( deviation > context->deviation );

#if defined _WIN32
    *tGpu = ts[0];
    *tCpu = ts[1] * m_qpcToNs;
#elif defined __linux__ && defined CLOCK_MONOTONIC_RAW
    *tGpu = ts[0];
    *tCpu = ts[1];
#else
    assert( false );
#endif
}

static inline TracyStromboliScope createTracyStromboliScope(TracyStromboliContext* context, struct SourceLocationData* srcloc, VkCommandBuffer cmdbuf, bool is_active) {
    TracyStromboliScope result = {
#ifdef TRACY_ON_DEMAND
        .active = is_active && GetProfiler().IsConnected(),
#else
        .active = is_active,
#endif
    };

    if( !result.active ) return result;
    result.commandBuffer = cmdbuf;
    result.context = context;

    const u32 queryId = nextQueryId(context);
    vkCmdWriteTimestamp( cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query, queryId);

    struct ___tracy_gpu_zone_begin_data params = {
        .srcloc = (u64)srcloc,
        .queryId = (u16)queryId,
        .context = context->context,
    };
    ___tracy_emit_gpu_zone_begin_serial(params);

    return result;
}


static inline TracyStromboliScope createTracyStromboliScopeWithCallstack(TracyStromboliContext* context, struct SourceLocationData* srcloc, VkCommandBuffer cmdbuf, int depth, bool is_active) {
    TracyStromboliScope result = {
#ifdef TRACY_ON_DEMAND
        .active = is_active && GetProfiler().IsConnected(),
#else
        .active = is_active,
#endif
    };

    if( !result.active ) return result;
    result.commandBuffer = cmdbuf;
    result.context = context;

    const u32 queryId = nextQueryId(context);
    vkCmdWriteTimestamp( cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query, queryId);

    struct ___tracy_gpu_zone_begin_callstack_data params = {
        .srcloc = (u64)srcloc,
        .queryId = (u16)queryId,
        .context = context->context,
        .depth = depth,
    };
    ___tracy_emit_gpu_zone_begin_callstack_serial(params);

    return result;
}

static inline TracyStromboliScope createTracyStromboliScopeAllocSource(TracyStromboliContext* context, uint32_t line, const char* source, size_t sourceSz, const char* function, size_t functionSz, const char* name, size_t nameSz, VkCommandBuffer cmdbuf, bool is_active) {
    TracyStromboliScope result = {
#ifdef TRACY_ON_DEMAND
        .active = is_active && GetProfiler().IsConnected(),
#else
        .active = is_active,
#endif
    };

    if( !result.active ) return result;
    result.commandBuffer = cmdbuf;
    result.context = context;

    const u32 queryId = nextQueryId(context);
    vkCmdWriteTimestamp( cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->query, queryId);

    u32 color = 0;
    const u64 srcloc = ___tracy_alloc_srcloc_name(line, source, sourceSz, function, functionSz, name, nameSz, color);
    struct ___tracy_gpu_zone_begin_data params = {
        .srcloc = (u64)srcloc,
        .queryId = (u16)queryId,
        .context = context->context,
    };
    ___tracy_emit_gpu_zone_begin_alloc_serial(params);

    return result;
}

static inline void destroyTracyStromboliScope(TracyStromboliScope* scope) {
    if( !scope->active ) return;

    const u32 queryId = nextQueryId(scope->context);
    vkCmdWriteTimestamp( scope->commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, scope->context->query, queryId );

    struct ___tracy_gpu_zone_end_data params = {
        .queryId = (u16)queryId,
        .context = scope->context->context,
    };
    ___tracy_emit_gpu_zone_end_serial(params);
}

#define TracyCVkContext(physdev, device, queue, cmdbuf, cmdpool) createTracyStromboliContext( physdev, device, queue, cmdbuf, cmdpool, 0, 0 );
#define TracyCVkContextCalibrated(physdev, device, queue, cmdbuf, cmdpool, gpdctd, gct) createTracyStromboliContext( physdev, device, queue, cmdbuf, cmdpool, gpdctd, gct );
#define TracyCVkDestroy(ctx) destroyTracyStromboliContext(ctx);
#define TracyCVkContextName( ctx, name, size )  tracyStromboliName(ctx, name, size);

#if defined TRACY_HAS_CALLSTACK && defined TRACY_CALLSTACK
#  define TracyCVkNamedZone( ctx_name, ctx, varname, cmdbuf, name, active ) static struct SourceLocationData TracyConcat(__tracy_gpu_source_location,__LINE__) = { name, __FUNCTION__,  __FILE__, (uint32_t)__LINE__, 0 }; TracyStromboliScope varname = createTracyStromboliScope( ctx, &TracyConcat(__tracy_gpu_source_location,__LINE__), cmdbuf, TRACY_CALLSTACK, active );
#  define TracyCVkNamedZoneC( ctx_name, ctx, varname, cmdbuf, name, color, active ) static struct SourceLocationData TracyConcat(__tracy_gpu_source_location,__LINE__) { name, __FUNCTION__,  __FILE__, (uint32_t)__LINE__, color }; TracyStromboliScope varname( ctx, &TracyConcat(__tracy_gpu_source_location,__LINE__), cmdbuf, TRACY_CALLSTACK, active );
#  define TracyCVkZone( ctx_name, ctx, cmdbuf, name ) TracyCVkNamedZoneS( ctx_name, ctx, ctx_name, cmdbuf, name, TRACY_CALLSTACK, true )
#  define TracyCVkZoneC( ctx_name, ctx, cmdbuf, name, color ) TracyCVkNamedZoneCS( ctx_name, ctx, ctx_name, cmdbuf, name, color, TRACY_CALLSTACK, true )
#  define TracyCVkZoneTransient( ctx, varname, cmdbuf, name, active ) TracyCVkZoneTransientS( ctx_name, ctx, varname, cmdbuf, name, TRACY_CALLSTACK, active )
#else
#  define TracyCVkNamedZone( ctx_name, ctx, varname, cmdbuf, name, active ) static struct SourceLocationData TracyConcat(__tracy_gpu_source_location,__LINE__) = { name, __FUNCTION__,  __FILE__, (uint32_t)__LINE__, 0 }; TracyStromboliScope varname = createTracyStromboliScope( ctx, &TracyConcat(__tracy_gpu_source_location,__LINE__), cmdbuf, active );
#  define TracyCVkNamedZoneC( ctx_name, ctx, varname, cmdbuf, name, color, active ) static struct SourceLocationData TracyConcat(__tracy_gpu_source_location,__LINE__) { name, __FUNCTION__,  __FILE__, (uint32_t)__LINE__, color }; TracyStromboliScope varname( ctx, &TracyConcat(__tracy_gpu_source_location,__LINE__), cmdbuf, active );
#  define TracyCVkZone( ctx_name, ctx, cmdbuf, name ) TracyCVkNamedZone( ctx_name, ctx, ctx_name, cmdbuf, name, true )
#  define TracyCVkZoneC( ctx_name, ctx, cmdbuf, name, color ) TracyCVkNamedZoneC( ctx_name, ctx, ctx_name, cmdbuf, name, color, true )
#  define TracyCVkZoneTransient( ctx_name, ctx, varname, cmdbuf, name, active ) TracyStromboliScope varname = createTracyStromboliScopeAllocSource( ctx, __LINE__, __FILE__, strlen( __FILE__ ), __FUNCTION__, strlen( __FUNCTION__ ), name, strlen( name ), cmdbuf, active );
#endif
#define TracyCVkCollect( ctx, cmdbuf ) tracyStromboliCollect(ctx, cmdbuf);
#define TracyCVkZoneEnd( ctx_name ) destroyTracyStromboliScope( &ctx_name );

#ifdef TRACY_HAS_CALLSTACK
#  define TracyCVkNamedZoneS( ctx_name, ctx, varname, cmdbuf, name, depth, active ) static struct SourceLocationData TracyConcat(__tracy_gpu_source_location,__LINE__) = { name, __FUNCTION__,  __FILE__, (uint32_t)__LINE__, 0 }; TracyStromboliScope varname = createTracyStromboliScopeWithCallstack( ctx, &TracyConcat(__tracy_gpu_source_location,__LINE__), cmdbuf, depth, active );
#  define TracyCVkNamedZoneCS( ctx_name, ctx, varname, cmdbuf, name, color, depth, active ) static struct SourceLocationData TracyConcat(__tracy_gpu_source_location,__LINE__) = { name, __FUNCTION__,  __FILE__, (uint32_t)__LINE__, color }; TracyStromboliScope varname = createTracyStromboliScopeWithCallstack( ctx, &TracyConcat(__tracy_gpu_source_location,__LINE__), cmdbuf, depth, active );
#  define TracyCVkZoneS( ctx_name, ctx, cmdbuf, name, depth ) TracyVkNamedZoneS( ctx, ___tracy_gpu_zone, cmdbuf, name, depth, true )
#  define TracyCVkZoneCS( ctx_name, ctx, cmdbuf, name, color, depth ) TracyVkNamedZoneCS( ctx, ___tracy_gpu_zone, cmdbuf, name, color, depth, true )
#  define TracyCVkZoneTransientS( ctx_name, ctx, varname, cmdbuf, name, depth, active ) TracyStromboliScope varname( ctx, __LINE__, __FILE__, strlen( __FILE__ ), __FUNCTION__, strlen( __FUNCTION__ ), name, strlen( name ), cmdbuf, depth, active );
#else
#  define TracyCVkNamedZoneS( ctx_name, ctx, varname, cmdbuf, name, depth, active ) TracyVkNamedZone( ctx, varname, cmdbuf, name, active )
#  define TracyCVkNamedZoneCS( ctx_name, ctx, varname, cmdbuf, name, color, depth, active ) TracyVkNamedZoneC( ctx, varname, cmdbuf, name, color, active )
#  define TracyCVkZoneS( ctx_name, ctx, cmdbuf, name, depth ) TracyVkZone( ctx, cmdbuf, name )
#  define TracyCVkZoneCS( ctx_name, ctx, cmdbuf, name, color, depth ) TracyVkZoneC( ctx, cmdbuf, name, color )
#  define TracyCVkZoneTransientS( ctx_name, ctx, varname, cmdbuf, name, depth, active ) TracyVkZoneTransient( ctx, varname, cmdbuf, name, active )
#endif




static void tracyEndCallback(TracyCZoneCtx* ctx) {
    TracyCZoneEnd(*ctx)
}
static void tracyVkEndCallback(TracyStromboliScope* scope) {
    destroyTracyStromboliScope(scope);
}

#define TRACY_ZONE_HELPER(name) TracyCZoneN(name, #name, true); __attribute__((__cleanup__(tracyEndCallback))) TracyCZoneCtx TracyConcat(name,__cb) = name;
#define TRACY_VK_ZONE_HELPER(ctx, cmdbuf, name) TracyCVkZone( TracyConcat(__tracy_vk_zone_,__LINE__), ctx, cmdbuf, name); __attribute__((__cleanup__(tracyVkEndCallback))) TracyStromboliScope TracyConcat(__cb_,__LINE__) = TracyConcat(__tracy_vk_zone_,__LINE__);

// Tracy helper function declarations
void initStromboliTracyContext(StromboliContext* context);
void destroyStromboliTracyContext(StromboliContext* context);
void collectTracyStromboli(VkCommandBuffer commandBuffer);
TracyStromboliContext* getTracyContext();

#endif // TRACY_ENABLE

#endif // STROMBOLI_TRACY_H