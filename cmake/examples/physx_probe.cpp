#include <PxPhysicsAPI.h>

int main()
{
    physx::PxVec3 axis(1.0f, 0.0f, 0.0f);
    return axis.x == 1.0f ? 0 : 1;
}
