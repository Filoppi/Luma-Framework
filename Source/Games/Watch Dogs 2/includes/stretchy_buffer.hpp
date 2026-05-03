#pragma once

class StretchyBuffer
{
public:
   StretchyBuffer(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t initial_capacity)
       : capacity(0),
         size(0)
   {
      this->device = device;
      Resize(context, initial_capacity);
   }

   void CopyFromBuffer(ID3D11DeviceContext* context, ID3D11Buffer* srcBuffer, uint32_t byteCount)
   {
      while (size + byteCount > capacity)
      {
         Resize(context, capacity * 2);
      }

      {
          D3D11_BOX srcBox = {};
          srcBox.left = 0;
          srcBox.top = 0;
          srcBox.front = 0;
          srcBox.right = byteCount;
          srcBox.bottom = 1;
          srcBox.back = 1;
          context->CopySubresourceRegion(buffer.get(), 0, size, 0, 0, srcBuffer, 0, &srcBox);
      }

      size += byteCount;
   }

   void Reset()
   {
      size = 0;
   }

   void Resize(ID3D11DeviceContext* context, uint32_t new_capacity)
   {
      com_ptr<ID3D11Buffer> old_buffer = buffer;

      {
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = new_capacity;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
         bd.StructureByteStride = 0;
         device->CreateBuffer(&bd, nullptr, &buffer);
      }
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
         srvd.Format = DXGI_FORMAT_R32_TYPELESS;
         srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
         srvd.Buffer.FirstElement = 0;
         srvd.Buffer.NumElements = new_capacity / 4;
         srvd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
         device->CreateShaderResourceView(buffer.get(), &srvd, &srv);
      }
      capacity = new_capacity;

      if (old_buffer)
      {
         D3D11_BOX srcBox = {};
         srcBox.left = 0;
         srcBox.top = 0;
         srcBox.front = 0;
         srcBox.right = size;
         srcBox.bottom = 1;
         srcBox.back = 1;
         context->CopySubresourceRegion(buffer.get(), 0, 0, 0, 0, old_buffer.get(), 0, &srcBox);
      }
   }

   com_ptr<ID3D11Device> device;
   com_ptr<ID3D11Buffer> buffer;
   com_ptr<ID3D11ShaderResourceView> srv;
   uint32_t capacity;
   uint32_t size;
};