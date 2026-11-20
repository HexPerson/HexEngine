-- Rotator.lua: spins its entity around an axis.
--
-- Attach with a ScriptComponent whose "script" field is
-- "GameData.Scripts/Rotator.lua". The Properties table is what the inspector
-- and the editor bridge (set_component_field) can edit per entity.

local Rotator = {
    Properties = {
        speed = 90,               -- degrees per second
        axis = Vec3(0, 1, 0),     -- rotation axis (parent space)
        reverseKey = "R",         -- press in play mode to reverse direction
    },
}

function Rotator:OnStart()
    self.direction = 1
    Log.Info("spinning", self.entity:GetName(), "at", self.speed, "deg/s")
end

function Rotator:OnKeyDown(key)
    if key == self.reverseKey then
        self.direction = -self.direction
    end
end

function Rotator:OnUpdate(dt)
    self.entity:Rotate(self.axis, self.speed * self.direction * dt)
end

return Rotator
