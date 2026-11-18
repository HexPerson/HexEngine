-- Bobber.lua: floats its entity up and down around where it started, and
-- glows brighter at the top if it has a light.

local Bobber = {
    Properties = {
        height = 0.5,       -- metres above/below the start position
        speed = 1.5,        -- cycles per second
        glow = true,        -- modulate a Light component on the same entity
    },
}

function Bobber:OnStart()
    self.origin = self.entity:GetPosition()
    self.light = self.entity:GetLight()   -- nil when there is none
    if self.light then
        self.baseStrength = self.light:GetStrength()
    end
end

function Bobber:OnUpdate(dt)
    local s = math.sin(Time.GetTime() * self.speed * 2 * math.pi)
    self.entity:SetPosition(self.origin + Vec3(0, s * self.height, 0))

    if self.glow and self.light then
        self.light:SetStrength(self.baseStrength * (1.25 + 0.25 * s))
    end
end

function Bobber:OnStop()
    -- Play mode restores the scene anyway; this is how you'd clean up.
    if self.light then
        self.light:SetStrength(self.baseStrength)
    end
end

return Bobber
