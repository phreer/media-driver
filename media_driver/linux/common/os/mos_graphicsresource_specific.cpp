/*
* Copyright (c) 2017-2021, Intel Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/
//!
//! \file     mos_graphicsresource_specific.cpp
//! \brief   Container class for the linux/Android specfic graphic resource
//!

#include "mos_defs.h"
#include "mos_util_debug.h"

#include "mos_graphicsresource_specific.h"
#include "mos_context_specific.h"
#include "memory_policy_manager.h"

GraphicsResourceSpecific::GraphicsResourceSpecific()
{
    MOS_OS_FUNCTION_ENTER;
}

GraphicsResourceSpecific::~GraphicsResourceSpecific()
{
    MOS_OS_FUNCTION_ENTER;
}

MOS_STATUS GraphicsResourceSpecific::SetSyncTag(OsContext* osContextPtr, SyncParams& params, uint32_t streamIndex)
{
    MOS_UNUSED(osContextPtr);
    MOS_UNUSED(params);
    MOS_UNUSED(streamIndex);
    return MOS_STATUS_SUCCESS;
}

bool GraphicsResourceSpecific::ResourceIsNull()
{
    return ((m_bo == nullptr)
#if (_DEBUG || _RELEASE_INTERNAL)
         && ((m_pData == nullptr) )
#endif // (_DEBUG || _RELEASE_INTERNAL)
    );

}

MOS_STATUS GraphicsResourceSpecific::Allocate(OsContext* osContextPtr, CreateParams& params)
{
    MOS_OS_FUNCTION_ENTER;

    if (osContextPtr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Unable to get the active OS context.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    if (osContextPtr->GetOsContextValid() == false)
    {
        MOS_OS_ASSERTMESSAGE("The OS context got is not valid.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    OsContextSpecific *pOsContextSpecific  = static_cast<OsContextSpecific *>(osContextPtr);

    MOS_STATUS         status          = MOS_STATUS_SUCCESS;
    uint32_t           tileFormatLinux = TILING_NONE;
    uint32_t           alignedHeight   = params.m_height;
    uint32_t           bufHeight       = params.m_height;
    GMM_RESOURCE_TYPE  resourceType    = RESOURCE_2D;
    int                mem_type        = MOS_MEMPOOL_VIDEOMEMORY;

    GMM_RESCREATE_PARAMS    gmmParams;
    MOS_ZeroMemory(&gmmParams, sizeof(gmmParams));

    switch (params.m_type)
    {
        case MOS_GFXRES_BUFFER:
        case MOS_GFXRES_SCRATCH:
          gmmParams.Type = RESOURCE_BUFFER;
          gmmParams.Flags.Gpu.State = true;
          alignedHeight = 1;
          break;

        case MOS_GFXRES_2D:
            gmmParams.Type = RESOURCE_2D;
            gmmParams.Flags.Gpu.Video = true;
            break;

        case MOS_GFXRES_VOLUME:
            gmmParams.Type = RESOURCE_3D;
            gmmParams.Flags.Gpu.Video = true;
            gmmParams.Depth = params.m_depth;
            break;

        default:
            MOS_OS_ASSERTMESSAGE("Unknown surface type");
            return MOS_STATUS_UNKNOWN;
    }

    // Create GmmResourceInfo
    gmmParams.Format = MosInterface::MosFmtToGmmFmt(params.m_format);
    if (gmmParams.Format == GMM_FORMAT_INVALID)
    {
        MOS_OS_ASSERTMESSAGE("Unsupported format");
        return MOS_STATUS_UNIMPLEMENTED;
    }
    gmmParams.BaseWidth = params.m_width;
    gmmParams.BaseHeight = alignedHeight;
    gmmParams.ArraySize = 1;

    MOS_TILE_TYPE tileformat = params.m_tileType;
    MOS_OS_NORMALMESSAGE("tilemode: tileformat = %d for %s, tileModeByForce = %d", tileformat, params.m_name.c_str(), params.m_tileModeByForce);
    switch (tileformat)
    {
        case MOS_TILE_Y:
            tileFormatLinux               = TILING_Y;
            if (params.m_isCompressible                                            &&
                MEDIA_IS_SKU(pOsContextSpecific->GetSkuTable(), FtrE2ECompression) &&
                MEDIA_IS_SKU(pOsContextSpecific->GetSkuTable(), FtrCompressibleSurfaceDefault))
            {
                gmmParams.Flags.Gpu.MMC = true;
                gmmParams.Flags.Info.MediaCompressed = 1;
                gmmParams.Flags.Gpu.CCS = 1;
                gmmParams.Flags.Gpu.RenderTarget = 1;
                gmmParams.Flags.Gpu.UnifiedAuxSurface = 1;

                if(MEDIA_IS_SKU(pOsContextSpecific->GetSkuTable(), FtrFlatPhysCCS))
                {
                    gmmParams.Flags.Gpu.UnifiedAuxSurface = 0;
                }
            }
            SetTileModebyForce(gmmParams, params.m_tileModeByForce);
            break;
        case MOS_TILE_X:
            gmmParams.Flags.Info.TiledX   = true;
            tileFormatLinux               = TILING_X;
            break;
        default:
            gmmParams.Flags.Info.Linear   = true;
            tileFormatLinux               = TILING_NONE;
    }

    if (nullptr != params.m_pSystemMemory)
    {
        // If user provides a system memory pointer, the gfx resource is backed
        // by the system memory pages. The resource is required to be linear.
        gmmParams.Flags.Info.Linear     = true;
        gmmParams.Flags.Info.Cacheable  = true;
        gmmParams.NoGfxMemory           = true;
        GMM_RESOURCE_INFO *tmpGmmResInfoPtr = pOsContextSpecific
                ->GetGmmClientContext()->CreateResInfoObject(&gmmParams);
        if (tmpGmmResInfoPtr == nullptr)
        {
            MOS_OS_ASSERTMESSAGE("Create GmmResInfo failed");
            return MOS_STATUS_UNKNOWN;
        }

        gmmParams.ExistingSysMemSize = GFX_ULONG_CAST(tmpGmmResInfoPtr->GetSizeSurface());
        gmmParams.pExistingSysMem = (GMM_VOIDPTR64)params.m_pSystemMemory;
        gmmParams.NoGfxMemory = false;
        gmmParams.Flags.Info.ExistingSysMem = true;

        pOsContextSpecific->GetGmmClientContext()
                ->DestroyResInfoObject(tmpGmmResInfoPtr);
    }
    else
    {
        gmmParams.Flags.Info.LocalOnly = MEDIA_IS_SKU(pOsContextSpecific->GetSkuTable(), FtrLocalMemory);
    }

    GMM_RESOURCE_INFO*  gmmResourceInfoPtr = pOsContextSpecific->GetGmmClientContext()->CreateResInfoObject(&gmmParams);

    if (gmmResourceInfoPtr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Get gmmResourceInfoPtr failed.");
        return MOS_STATUS_INVALID_PARAMETER;
    }

    switch (gmmResourceInfoPtr->GetTileType())
    {
        case GMM_TILED_X:
            tileformat      = MOS_TILE_X;
            tileFormatLinux = TILING_X;
            break;
        case GMM_TILED_Y:
            tileformat      = MOS_TILE_Y;
            tileFormatLinux = TILING_Y;
            break;
        case GMM_NOT_TILED:
            tileformat      = MOS_TILE_LINEAR;
            tileFormatLinux = TILING_NONE;
            break;
        default:
            tileformat      = MOS_TILE_Y;
            tileFormatLinux = TILING_Y;
            break;
    }

    if (params.m_tileType== MOS_TILE_Y)
    {
        gmmResourceInfoPtr->SetMmcMode((GMM_RESOURCE_MMC_INFO)params.m_compressionMode, 0);
    }

    uint32_t bufPitch        = GFX_ULONG_CAST(gmmResourceInfoPtr->GetRenderPitch());
    uint32_t bufSize         = GFX_ULONG_CAST(gmmResourceInfoPtr->GetSizeSurface());
    bufHeight                = gmmResourceInfoPtr->GetBaseHeight();
    MOS_LINUX_BO* boPtr      = nullptr;

    char bufName[m_maxBufNameLength];
    MOS_SecureStrcpy(bufName, m_maxBufNameLength, params.m_name.c_str());

    if(!params.m_pSystemMemory)
    {
        MemoryPolicyParameter memPolicyPar;
        MOS_ZeroMemory(&memPolicyPar, sizeof(MemoryPolicyParameter));

        memPolicyPar.skuTable = pOsContextSpecific->GetSkuTable();
        memPolicyPar.waTable  = pOsContextSpecific->GetWaTable();
        memPolicyPar.resInfo  = gmmResourceInfoPtr;
        memPolicyPar.resName  = params.m_name.c_str();
        memPolicyPar.preferredMemType = params.m_memType;

        mem_type = MemoryPolicyManager::UpdateMemoryPolicy(&memPolicyPar);
    }

    MOS_TraceEventExt(EVENT_RESOURCE_ALLOCATE, EVENT_TYPE_START, nullptr, 0, nullptr, 0);
    if (nullptr != params.m_pSystemMemory)
    {
        struct mos_drm_bo_alloc_userptr alloc_uptr;
        alloc_uptr.name = bufName;
        alloc_uptr.addr = params.m_pSystemMemory;
        alloc_uptr.tiling_mode = tileFormatLinux;
        alloc_uptr.stride = bufPitch;
        alloc_uptr.size = bufSize;

        boPtr = mos_bo_alloc_userptr(pOsContextSpecific->m_bufmgr, &alloc_uptr);
    }
    // Only Linear and Y TILE supported
    else if (tileFormatLinux == TILING_NONE)
    {
        struct mos_drm_bo_alloc alloc;
        alloc.name = bufName;
        alloc.size = bufSize;
        alloc.alignment = 4096;
        alloc.ext.mem_type = mem_type;
        boPtr = mos_bo_alloc(pOsContextSpecific->m_bufmgr, &alloc);
    }
    else
    {
        struct mos_drm_bo_alloc_tiled alloc_tiled;
        alloc_tiled.name = bufName;
        alloc_tiled.x = bufPitch;
        alloc_tiled.y = bufSize/bufPitch;
        alloc_tiled.cpp = 1;
        alloc_tiled.ext.tiling_mode = tileFormatLinux;
        alloc_tiled.ext.mem_type = mem_type;

        boPtr = mos_bo_alloc_tiled(pOsContextSpecific->m_bufmgr,
                        &alloc_tiled);

        bufPitch = (uint32_t)alloc_tiled.pitch;
    }

    m_mapped = false;
    if (boPtr)
    {
        m_format   = params.m_format;
        m_width    = params.m_width;
        m_height   = bufHeight;
        m_pitch    = bufPitch;
        m_count    = 0;
        m_bo       = boPtr;
        m_name     = params.m_name;
        m_pData    = (uint8_t*) boPtr->virt;

        m_gmmResInfo    = gmmResourceInfoPtr;
        m_mapped        = false;
        m_mmapOperation = MOS_MMAP_OPERATION_NONE;

        m_arraySize = 1;
        m_depth     = MOS_MAX(1, gmmResourceInfoPtr->GetBaseDepth());
        m_size      = (uint32_t)gmmResourceInfoPtr->GetSizeSurface();
        m_tileType  = tileformat;
        m_tileModeGMM           = (MOS_TILE_MODE_GMM)gmmResourceInfoPtr->GetTileModeSurfaceState();
        m_isGMMTileEnabled      = true;

        m_compressible    = gmmParams.Flags.Gpu.MMC ?
            (gmmResourceInfoPtr->GetMmcHint(0) == GMM_MMC_HINT_ON) : false;
        m_isCompressed    = gmmResourceInfoPtr->IsMediaMemoryCompressed(0);
        m_compressionMode = (MOS_RESOURCE_MMC_MODE)gmmResourceInfoPtr->GetMmcMode(0);

        m_memObjCtrlState = MosInterface::GetCachePolicyMemoryObject(
            pOsContextSpecific->GetGmmClientContext(),
            params.m_mocsMosResUsageType);
        m_mocsMosResUsageType = params.m_mocsMosResUsageType;

        MOS_OS_VERBOSEMESSAGE("Alloc %7d bytes (%d x %d resource), tile encoding %d.",bufSize, params.m_width, bufHeight, m_tileModeGMM);

        struct {
            uint32_t m_handle;
            uint32_t m_resFormat;
            uint32_t m_baseWidth;
            uint32_t m_baseHeight;
            uint32_t m_pitch;
            uint32_t m_size;
            uint32_t m_resTileType;
            GMM_RESOURCE_FLAG m_resFlag;
            uint32_t          m_reserve;
        } eventData;

        eventData.m_handle       = boPtr->handle;
        eventData.m_baseWidth    = m_width;
        eventData.m_baseHeight   = m_height;
        eventData.m_pitch        = m_pitch;
        eventData.m_size         = m_size;
        eventData.m_resFormat    = m_format;
        eventData.m_resTileType  = m_tileType;
        eventData.m_resFlag      = gmmResourceInfoPtr->GetResFlags();
        eventData.m_reserve      = 0;
        MOS_TraceEventExt(EVENT_RESOURCE_ALLOCATE,
            EVENT_TYPE_INFO,
            &eventData,
            sizeof(eventData),
            params.m_name.c_str(),
            params.m_name.size() + 1);
    }
    else
    {
        MOS_OS_ASSERTMESSAGE("Fail to Alloc %7d bytes (%d x %d resource).",bufSize, params.m_width, params.m_height);
        status = MOS_STATUS_NO_SPACE;
    }
    MOS_TraceEventExt(EVENT_RESOURCE_ALLOCATE, EVENT_TYPE_END, &status, sizeof(status), nullptr, 0);

    m_memAllocCounterGfx++;
    return  status;
}

void GraphicsResourceSpecific::Free(OsContext* osContextPtr, uint32_t  freeFlag)
{
    MOS_OS_FUNCTION_ENTER;

    MOS_UNUSED(osContextPtr);
    MOS_UNUSED(freeFlag);

    OsContextSpecific *pOsContextSpecific = static_cast<OsContextSpecific *>(osContextPtr);

    MOS_LINUX_BO* boPtr = m_bo;

    if (boPtr)
    {
        AuxTableMgr *auxTableMgr = pOsContextSpecific->GetAuxTableMgr();
        if (auxTableMgr)
        {
            auxTableMgr->UnmapResource(m_gmmResInfo, boPtr);
        }
        mos_bo_unreference(boPtr);
        m_bo = nullptr;
        if (nullptr != m_gmmResInfo)
        {
            pOsContextSpecific->GetGmmClientContext()->DestroyResInfoObject(m_gmmResInfo);
            m_gmmResInfo = nullptr;
            m_memAllocCounterGfx--;
        }
    }
    return;
}

bool GraphicsResourceSpecific::IsEqual(GraphicsResource* toCompare)
{
    if  (toCompare == nullptr)
    {
        return false;
    }

    GraphicsResourceSpecific *resSpecificPtr = static_cast<GraphicsResourceSpecific *>(toCompare);

    return (m_bo == resSpecificPtr->m_bo);
}

bool GraphicsResourceSpecific::IsValid()
{
    return (m_bo != nullptr);
}

MOS_STATUS GraphicsResourceSpecific::ConvertToMosResource(MOS_RESOURCE* pMosResource)
{
    if (pMosResource == nullptr)
    {
        return MOS_STATUS_INVALID_PARAMETER;
    }

    pMosResource->Format   = m_format;
    pMosResource->iWidth   = m_width;
    pMosResource->iHeight  = m_height;
    pMosResource->iPitch   = m_pitch;
    pMosResource->iDepth   = m_depth;
    pMosResource->TileType = m_tileType;
    pMosResource->TileModeGMM = m_tileModeGMM;
    pMosResource->bGMMTileEnabled = m_isGMMTileEnabled;
    pMosResource->iCount   = 0;
    pMosResource->pData    = m_pData;
    pMosResource->bufname  = m_name.c_str();
    pMosResource->bo       = m_bo;
    pMosResource->bMapped  = m_mapped;
    pMosResource->MmapOperation = m_mmapOperation;
    pMosResource->pGmmResInfo   = m_gmmResInfo;

    pMosResource->user_provided_va  = m_userProvidedVA;

    pMosResource->memObjCtrlState   = m_memObjCtrlState;
    pMosResource->mocsMosResUsageType = m_mocsMosResUsageType;

    pMosResource->pGfxResource    = this;

    return  MOS_STATUS_SUCCESS;
}

void* GraphicsResourceSpecific::Lock(OsContext* osContextPtr, LockParams& params)
{
    MOS_OS_FUNCTION_ENTER;

    if (osContextPtr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Unable to get the active OS context.");
        return nullptr;
    }

    if (osContextPtr ->GetOsContextValid() == false)
    {
        MOS_OS_ASSERTMESSAGE("The OS context got is not valid.");
        return nullptr;
    }

    OsContextSpecific *pOsContextSpecific  = static_cast<OsContextSpecific *>(osContextPtr);

    void*   dataPtr     = nullptr;
    MOS_LINUX_BO* boPtr = m_bo;

    if (boPtr)
    {
        // Do decompression for a compressed surface before lock
        const auto pGmmResInfo = m_gmmResInfo;
        MOS_OS_ASSERT(pGmmResInfo);
        GMM_RESOURCE_FLAG GmmFlags = pGmmResInfo->GetResFlags();
        
        if (!params.m_noDecompress &&
            (((GmmFlags.Gpu.MMC || GmmFlags.Gpu.CCS) && GmmFlags.Info.MediaCompressed) ||
             pGmmResInfo->IsMediaMemoryCompressed(0)))
        {
            if ((pOsContextSpecific->m_mediaMemDecompState == nullptr) ||
                (pOsContextSpecific->m_memoryDecompress    == nullptr))
            {
                MOS_OS_ASSERTMESSAGE("m_mediaMemDecompState/m_memoryDecompress is not valid.");
                return nullptr;
            }

            MOS_RESOURCE mosResource = {};
            ConvertToMosResource(&mosResource);
            pOsContextSpecific->m_memoryDecompress(pOsContextSpecific->m_mosContext, &mosResource);
        }

        if(false == m_mapped)
        {
            if (pOsContextSpecific->IsAtomSoc())
            {
                mos_bo_map_gtt(boPtr);
            }
            else
            {
                if (m_tileType != MOS_TILE_LINEAR && !params.m_tileAsTiled)
                {
                    if (pOsContextSpecific->UseSwSwizzling())
                    {
                        mos_bo_map(boPtr, ( OSKM_LOCKFLAG_WRITEONLY & params.m_writeRequest ));
                        m_mmapOperation = MOS_MMAP_OPERATION_MMAP;
                        if (m_systemShadow == nullptr)
                        {
                            m_systemShadow = (uint8_t *)MOS_AllocMemory(boPtr->size);
                            MOS_OS_CHECK_CONDITION((m_systemShadow == nullptr), "Failed to allocate shadow surface", nullptr);
                        }
                        if (m_systemShadow)
                        {
                            int32_t flags = pOsContextSpecific->GetTileYFlag() ? 0 : 1;
                            uint64_t surfSize = m_gmmResInfo->GetSizeMainSurface();
                            MOS_OS_CHECK_CONDITION((m_tileType != MOS_TILE_Y), "Unsupported tile type", nullptr);
                            MOS_OS_CHECK_CONDITION((boPtr->size <= 0 || m_pitch <= 0), "Invalid BO size or pitch", nullptr);
                            Mos_SwizzleData((uint8_t*)boPtr->virt, m_systemShadow,
                                            MOS_TILE_Y, MOS_TILE_LINEAR,
                                            (int32_t)(surfSize / m_pitch), m_pitch, flags);
                        }
                    }
                    else
                    {
                        mos_bo_map_gtt(boPtr);
                        m_mmapOperation = MOS_MMAP_OPERATION_MMAP_GTT;
                    }
                }
                else if (params.m_uncached)
                {
                    mos_bo_map_wc(boPtr);
                    m_mmapOperation = MOS_MMAP_OPERATION_MMAP_WC;
                }
                else
                {
                    mos_bo_map(boPtr, ( OSKM_LOCKFLAG_WRITEONLY & params.m_writeRequest ));
                    m_mmapOperation = MOS_MMAP_OPERATION_MMAP;
                }
            }
            m_mapped = true;
            m_pData  = m_systemShadow ? m_systemShadow : (uint8_t *)boPtr->virt;
        }

        dataPtr = m_pData;
    }

    MOS_OS_ASSERT(dataPtr);
    return dataPtr;
}

MOS_STATUS GraphicsResourceSpecific::Unlock(OsContext* osContextPtr)
{
    MOS_OS_FUNCTION_ENTER;

    if (osContextPtr == nullptr)
    {
        MOS_OS_ASSERTMESSAGE("Unable to get the active OS context.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    if (osContextPtr ->GetOsContextValid() == false)
    {
        MOS_OS_ASSERTMESSAGE("The OS context got is not valid.");
        return MOS_STATUS_INVALID_HANDLE;
    }

    OsContextSpecific *pOsContextSpecific  = static_cast<OsContextSpecific *>(osContextPtr);

    MOS_LINUX_BO* boPtr = m_bo;
    if (boPtr)
    {
        if (m_mapped)
        {
           if (pOsContextSpecific->IsAtomSoc())
           {
               mos_bo_unmap_gtt(boPtr);
           }
           else
           {
               if (m_systemShadow)
               {
                   int32_t flags = pOsContextSpecific->GetTileYFlag() ? 0 : 1;
                   uint64_t surfSize = m_gmmResInfo->GetSizeMainSurface();
                   Mos_SwizzleData(m_systemShadow, (uint8_t*)boPtr->virt,
                                   MOS_TILE_LINEAR, MOS_TILE_Y,
                                   (int32_t)(surfSize / m_pitch), m_pitch, flags);
                   MOS_FreeMemory(m_systemShadow);
                   m_systemShadow = nullptr;
               }

               switch(m_mmapOperation)
               {
                   case MOS_MMAP_OPERATION_MMAP_GTT:
                        mos_bo_unmap_gtt(boPtr);
                        break;
                   case MOS_MMAP_OPERATION_MMAP_WC:
                        mos_bo_unmap_wc(boPtr);
                        break;
                   case MOS_MMAP_OPERATION_MMAP:
                        mos_bo_unmap(boPtr);
                        break;
                   default:
                        MOS_OS_ASSERTMESSAGE("Invalid mmap operation type");
                        break;
               }
            }

            m_mapped           = false;
            m_mmapOperation    = MOS_MMAP_OPERATION_NONE;

            boPtr->virt        = nullptr;
            m_bo = boPtr;
        }

        m_pData = nullptr;
    }

    return MOS_STATUS_SUCCESS;
}

MOS_STATUS GraphicsResourceSpecific::SetTileModebyForce(GMM_RESCREATE_PARAMS &gmmParams, MOS_TILE_MODE_GMM tileMode)
{
    if (tileMode == MOS_TILE_64_GMM)
    {
        gmmParams.Flags.Info.Tile64 = true;
    }
    else if (tileMode == MOS_TILE_4_GMM)
    {
        gmmParams.Flags.Info.Tile4 = true;
    }
    return MOS_STATUS_SUCCESS;
}
