// Declaration-only C++ syntax smoke. NOT a CUDA SDK or ABI/runtime test.
#pragma once
#include <cstddef>
using CUresult=int;using CUdevice=int;using CUdeviceptr=unsigned long long;
struct Ctx;struct Mod;struct Str;struct Eve;struct Fun;
using CUcontext=Ctx*;using CUmodule=Mod*;using CUstream=Str*;using CUevent=Eve*;using CUfunction=Fun*;
struct CUuuid{char bytes[16];};
enum{CUDA_SUCCESS=0,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR=1,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR=2,CU_STREAM_NON_BLOCKING=1,CU_EVENT_DEFAULT=0,CU_FUNC_ATTRIBUTE_NUM_REGS=3,CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES=4,CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES=5,CU_FUNC_ATTRIBUTE_BINARY_VERSION=6};
extern "C" {
CUresult cuInit(unsigned);CUresult cuDeviceGet(CUdevice*,int);CUresult cuDeviceGetAttribute(int*,int,CUdevice);
CUresult cuDevicePrimaryCtxRetain(CUcontext*,CUdevice);CUresult cuDevicePrimaryCtxRelease(CUdevice);CUresult cuCtxSetCurrent(CUcontext);
CUresult cuGetErrorString(CUresult,const char**);CUresult cuDeviceGetLuid(char*,unsigned*,CUdevice);CUresult cuDeviceGetUuid(CUuuid*,CUdevice);CUresult cuDeviceGetName(char*,int,CUdevice);
CUresult cuMemAlloc(CUdeviceptr*,std::size_t);CUresult cuMemFree(CUdeviceptr);CUresult cuMemHostAlloc(void**,std::size_t,unsigned);CUresult cuMemFreeHost(void*);
CUresult cuMemsetD8(CUdeviceptr,unsigned char,std::size_t);CUresult cuMemsetD8Async(CUdeviceptr,unsigned char,std::size_t,CUstream);
CUresult cuMemcpyDtoH(void*,CUdeviceptr,std::size_t);CUresult cuMemcpyHtoD(CUdeviceptr,const void*,std::size_t);
CUresult cuMemcpyDtoHAsync(void*,CUdeviceptr,std::size_t,CUstream);CUresult cuMemcpyHtoDAsync(CUdeviceptr,const void*,std::size_t,CUstream);
CUresult cuStreamCreate(CUstream*,unsigned);CUresult cuStreamDestroy(CUstream);CUresult cuStreamSynchronize(CUstream);
CUresult cuModuleLoadData(CUmodule*,const void*);CUresult cuModuleUnload(CUmodule);CUresult cuModuleGetFunction(CUfunction*,CUmodule,const char*);CUresult cuModuleGetGlobal(CUdeviceptr*,std::size_t*,CUmodule,const char*);
CUresult cuFuncGetAttribute(int*,int,CUfunction);CUresult cuOccupancyMaxActiveBlocksPerMultiprocessor(int*,CUfunction,int,std::size_t);
CUresult cuEventCreate(CUevent*,unsigned);CUresult cuEventDestroy(CUevent);CUresult cuEventRecord(CUevent,CUstream);CUresult cuEventElapsedTime(float*,CUevent,CUevent);
CUresult cuLaunchKernel(CUfunction,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,CUstream,void**,void**);
}

extern "C" CUresult cuDriverGetVersion(int*);
extern "C" CUresult cuDeviceGetPCIBusId(char*,int,CUdevice);
extern "C" CUresult cuMemAllocHost(void**,std::size_t);
