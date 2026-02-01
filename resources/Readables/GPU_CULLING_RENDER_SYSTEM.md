# **GPU Occlusion & Frustum Culling System**

## **1\. High-Level Overview**

This system moves the burden of deciding "what to draw" from the CPU to the GPU. In a traditional engine, the CPU iterates over every chunk, checks if it's in the camera's view, and issues a draw call. This is slow (CPU bottlenecks).  
In this system, we upload **all** chunk metadata (AABBs, vertex counts) to the GPU once. Every frame, a **Compute Shader** analyzes every chunk in parallel, checking if it's visible. It builds a list of draw commands directly on the GPU, which are then executed via **Indirect Drawing**.

### **The Pipeline Diagram**

graph TD  
    subgraph "Initialization"  
    A\[World Gen / Chunk Load\] \--\>|AddOrUpdateChunk| B\[GPU Buffer: Global Chunk Data\]  
    B \--\>|Contains AABB & Vertex Info| C{VRAM Storage}  
    end

    subgraph "Per Frame Render Loop"  
    D\[Depth Buffer from Prev Frame\] \--\>|GenerateHiZ| E\[Hi-Z Depth Pyramid\]  
      
    E \--\> F\[Compute Shader: CULLING\]  
    C \--\> F  
    G\[Camera Matrices\] \--\> F  
      
    F \--\>|Frustum & Occlusion Checks| H\[Decide Visibility\]  
      
    H \--\>|Visible? Yes| I\[Atomic Counter \++\]  
    H \--\>|Visible? Yes| J\[Write to Indirect Command Buffer\]  
    H \--\>|Visible? Yes| K\[Write to Visible Instance Buffer\]  
      
    I \--\> L\[DrawIndirect\]  
    J \--\> L  
    K \--\> L  
      
    L \--\>|One Draw Call| M\[Rasterizer / Screen\]  
    end

## **2\. Integration Guide (How to plug it in)**

### **Prerequisites**

1. **Modern OpenGL (4.5+)**: Requires glMultiDrawArraysIndirectCount and glDispatchCompute.  
2. **Shaders**: You need CULL\_COMPUTE.glsl and HI\_Z\_DOWN.glsl.  
3. **Dependencies**: GLM (math), GLAD (loader), GLFW (context).

### **Step-by-Step Setup**

**1\. Initialization**  
Initialize the culler *after* your OpenGL context is created.  
// Allow for 10,000 chunks max (adjust based on world size)  
GpuCuller\* culler \= new GpuCuller(10000); 

**2\. Loading Data (When mesh is created)**  
When you generate a chunk mesh, tell the culler about it. You get back a slot ID, but you usually map your ChunkID (int64) to this internally, which the class handles for you.  
// Inside ChunkManager::UploadMesh  
culler-\>AddOrUpdateChunk(  
    chunkPtr-\>id,       // Unique ID  
    chunkMinAABB,       // Bottom-left corner  
    chunkMaxAABB,       // Top-right corner  
    1.0f,               // Scale  
    meshFirstVertex,    // Offset in big vertex buffer  
    meshVertexCount     // Number of vertices  
);

**3\. Unloading Data (When chunk unloads)**  
culler-\>RemoveChunk(chunkPtr-\>id);

**4\. The Render Loop**  
This is the critical sequence. It must happen *before* you render the opaque geometry if you want to use the result immediately, OR you use the depth buffer from the *previous* frame (temporal coherence) for occlusion.  
void RenderFrame() {  
    // 1\. Generate Hi-Z Buffer (Downsample Depth)  
    // Requires the depth texture from the G-Buffer or Forward pass  
    culler-\>GenerateHiZ(gDepthTexture, SCR\_WIDTH, SCR\_HEIGHT);

    // 2\. Run Compute Shader  
    // Checks visibility and fills the Indirect Buffer  
    culler-\>Cull(viewProjMatrix, prevViewProj, projMatrix, gDepthTexture);

    // 3\. Draw  
    // Binds the indirect buffer and executes the draw  
    shader-\>use(); // Your chunk shader  
    culler-\>DrawIndirect(emptyVAO);   
}

## **3\. Detailed Workflow Explanations**

### **A. AddOrUpdateChunk & Data Management**

* **The Problem**: Constant glBufferSubData calls are slow if done randomly.  
* **The Solution**: The class maintains a m\_globalChunkBuffer. This is a massive SSBO (Shader Storage Buffer Object) residing in VRAM. It acts like an array of ChunkGpuData structs.  
* **Slot Management**: We use a stack\<uint32\_t\> m\_freeSlots to quickly find an empty index in this array. We map ChunkID \-\> SlotIndex using a hash map.  
* **Action**: When called, it finds a slot, packs the AABB and vertex info into a struct, and uploads *only* that struct to the specific offset in VRAM.

### **B. GenerateHiZ (Hierarchical Z-Buffer)**

* **Concept**: To check if a chunk is hidden behind a mountain, we need to check the depth buffer. Checking every pixel of the chunk against the depth buffer is too slow.  
* **Algorithm**: We create a "mipmap chain" of the depth buffer. Level 0 is full res. Level 1 is half res (taking the *max* depth of the 4 pixels it covers).  
* **Usage**: If a chunk projects to a 100x100 pixel area on screen, we don't check Level 0\. We check a higher mip level where that 100x100 area is represented by just \~4 pixels. This allows O(1) occlusion queries.

### **C. Cull (The Compute Shader)**

* **Input**: The list of all chunks (m\_globalChunkBuffer).  
* **Process**:  
  1. **Frustum Culling**: Is the box inside the camera frustum?  
  2. **Occlusion Culling**: Projects the box to screen space. Samples the Hi-Z texture. If the box is further away than the depth value in the Hi-Z texture, it is occluded (hidden).  
* **Output**:  
  1. **Visible Instance Buffer**: Stores the index/matrix of the visible chunk.  
  2. **Indirect Command Buffer**: Writes a DrawArraysIndirectCommand struct (count, instanceCount, first, baseInstance).  
  3. **Atomic Counter**: Integers that increment atomically to keep track of how many items passed the test.

### **D. DrawIndirect**

* **Magic**: Instead of calling glDrawArrays 1000 times, we call glMultiDrawArraysIndirectCount.  
* **Mechanism**: OpenGL reads the command buffer generated by the compute shader in step C. It sees "Draw 300 vertices starting at index 500" and executes it. It does this for every command in the buffer.  
* **Efficiency**: This reduces driver overhead to almost zero.