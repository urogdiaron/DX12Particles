# DX12Particles
Attempt to create a simple 2D particle system while learning the craziness that is DX12.

For proper usage of the swap chain we need one of these for every frame buffer:
    - A command allocator (command lists can be reused so one is enough)
    - A particle buffer containing position and velocity data.
        Note: To ping-pong between two buffers we need the data produced by the PREVIOUS frame held by a different FrameResource.
        Seems like we need to wait for the results of the previous frame after all. 
        So I'm not so sure about the benefits of buffered rendering.
    - A constant buffer that can be on the upload heap always mapped

Alright here's the plan:

For the first try we don't bother with multiple frame buffers keeping everything as simple as possible.
Hopefully I'll get round to revising this.

Initialization:
    - Create the device, swapbuffer, the render targets, the default stuff
    - Make an allocator for the draw calls and on for the dispatch calls.
    - Make two command lists, one for each allocator. (Since we need to provide compute or direct type for the list at creation time, I can't use just one.)
    - Create two particle buffers containing PARTICLE structs. (TODO: see if a SOA approach is better than an AOS one)
    - Create a single vertex buffer containing ParticleCount elements of dummy color data. (TODO: do we need this? Probably not.)

Per frame:
    - Make sure the previous frame executed completely on the GPU
    - Reset the graphics command allocator, the compute allocator and command list.
    - Set one particle buffer as the input (SRV), one as the output (UAV).
    - Dispatch
    - Wait for the Dispatch to execute on the GPU. The CPU doesn't need to wait, so use the Wait function on the queue and not the event based stuff.
    - Set the output of the compute pass as the input particle buffer using a barrier.
    - Draw the pointlist that only contains color data (TODO: check if we can omit even that).
    - Use the geometry shader to make a quad from the point data

Shader:
    - Vertex buffer outputs the position from the particle buffer based on the vertex id.
    - Geometry shader makes a screen space quad
    - Pixel shader returns red.
    - Compute shader
        - for now it just puts the particles in an evenly spaced static grid based on GroupID and Group Thread ID.