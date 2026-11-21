#pragma once

#include <string>

namespace HexEngine
{
	// Read-mostly view of a city-simulation routine agent (implemented by the
	// CitySimulation plugin's RoutineAgentComponent), so game code can ask where
	// an NPC is in its day without linking the plugin. Look it up with
	// Entity::GetComponentDerived<IRoutineAgentState>() (a dynamic_cast cross-cast).
	class IRoutineAgentState
	{
	public:
		virtual ~IRoutineAgentState() = default;

		// Venue entity the agent has ARRIVED at and is staying at (its workplace,
		// home or a visited venue); empty while travelling or before its first
		// arrival.
		virtual const std::string& GetSettledVenueName() const = 0;
		virtual const std::string& GetHomeEntityName() const = 0;
		virtual const std::string& GetAssignedWorkplaceEntityName() const = 0;

		// True when the agent's assigned workplace offers `tag` as a visitor
		// service (e.g. "Food" for a takeaway kitchen). Case-insensitive.
		virtual bool WorkplaceOffersService(const std::string& tag) const = 0;

		// Hold the agent at its current venue: while held and settled it keeps
		// pottering on site and starts no new trip (schedule changes wait). Used
		// e.g. to keep a job's pickup NPC findable until the player collects.
		virtual void SetHeldAtVenue(bool held) = 0;
		virtual bool IsHeldAtVenue() const = 0;

		// Freeze the agent where it stands (e.g. while the player talks to it):
		// navigation stops mid-path, its routine clock and walk timeouts don't
		// advance, and it idles. Releasing resumes the same trip.
		virtual void SetConversationPaused(bool paused) = 0;
		virtual bool IsConversationPaused() const = 0;
	};
}
