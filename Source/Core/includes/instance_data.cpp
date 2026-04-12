#if 0 // TODO!!!
#include "instance_data.h"

#if DEVELOPMENT
void CommandListData::AddGraphicsCaptureCustomDrawCall(const char* name, ID3D11DeviceContext* native_device_context, ID3D11Resource* resource, ID3D11View* resource_view)
{
   const std::shared_lock lock_trace(s_mutex_trace);
   if (trace_running)
   {
      const std::unique_lock lock_trace_2(mutex_trace);
      TraceDrawCallData trace_draw_call_data;
      trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
      trace_draw_call_data.command_list = native_device_context;
      trace_draw_call_data.custom_name = name;
      // Re-use the Resource or View data for simplicity
      if (resource_view)
         GetResourceInfo(resource_view, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
      else if (resource)
         GetResourceInfo(resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);

      trace_draw_calls_data.insert(trace_draw_calls_data.end() - 1, trace_draw_call_data);
   }
   trace_draw_calls_data;
}
#endif
#endif