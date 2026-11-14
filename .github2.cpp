/*
LUAU + BIT OF LUA FOR BOT MECHANICS + AI WORKAROUND
	- DM FOR COLLABORATION 
		- perhaps even machine learning????
*/




-- [[
	Bots that fill quiet servers so a lone player can still play a proper round.
		1 real player  -> 7 bots
		2 real players -> 4 bots
		3 + players     -> no bots (humans only)
		(Set the round script's "BotsEnabled" attribute to false to turn bots off.)

			A bot looks like a player to the round script(MurderMysteryGame) : it has a Name, a Character,
			a "Backpack" for its weapons, and GetAttribute / SetAttribute, so it gets a role, a disguise,
			weapons, an alive count, a body when it dies and a line on the end screen just like a person.
			Bot rounds pay real players half the usual credits and XP (the round script does that).

			How they behave (normal difficulty) :
			Innocents  wander the map, stop and look around, now and then jump. Once they know who the
			murderer is (they saw the knife out, or saw a kill) they run away from them.
			They go and grab a dropped gun if they see it.
			Sheriff    wanders too (sometimes with the gun out). When it sees the murderer it stops, takes
			a moment to react, aims and shoots - not perfectly : further away = more misses.
			Same rule as players : shooting an innocent kills the sheriff too.
			Murderer   blends in until it has the knife, then picks someone (often whoever is alone or
				nearest), stalks them, pulls the knife out when close and slashes, or throws the
			knife from a distance. After a kill it puts the knife away and walks off.
			Everything they "see" is checked with line of sight and a field of view, like a person's eyes.

			BotService.countFor(playeras)           -> how many bots to add
			BotService.create(count)               -> list of bot objects (no bodies yet)
			BotService.spawnCharacter(bot, cframe) -> body for the round
			BotService.isBot(p) / fromCharacter(model) / characters()
			BotService.startRound(ctx)             -> brains on (ctx = participants, roles, inRound, murderer, spawnParts)
			BotService.endRound()                  -> brains off, bodies removed

			COMPETITION matches (CompetitionService) run their own bots at the same time as the main round:
	BotService.create(count, group)        -> bots for one match (group = the match's id)
		BotService.startRound(ctx)             -> with ctx.group + ctx.teams ([player] = "Murderer" / "Sheriff") :
		Murderer - team bots knife - fight, Sheriff - team bots gunfight,
		and they only go for the other team; ctx.isLive() says when
		weapons work (the main round's RoundActive is ignored)
			BotService.endRound(ctx)               -> only that match's bots leave (endRound() = the main round's only)
		BotService.respawn(bot, cframe)        -> a fresh body for a bot already playing (the next competition round)
]]
	local Debris = game:GetService("Debris")
		local PathfindingService = game : GetService("PathfindingService")
		local Players = game : GetService("Players")
		local ReplicatedStorage = game : GetService("ReplicatedStorage")
		local RunService = game : GetService("RunService")
		local StarterPlayer = game : GetService("StarterPlayer")

		local BotService = {}

		local KNIFE_NAME = "TMM_KNIFE"
		local GUN_NAME = "TMM_REVOLVER"

		--how many bots for how many real players
		local BOTS_FOR_PLAYERS = { [1] = 7,[2] = 4 }

		--Normal difficulty
		local SIGHT_RANGE = 85 --how far they can see
		local FIELD_OF_VIEW = 0.15 --dot product : about 160 degrees in front of them
		local HEAR_RANGE = 12 -- this close they notice you even behind them
		local REACTION_MIN, REACTION_MAX = 0.45, 0.95 --seconds before reacting to something new
		local SHERIFF_RANGE = 90
		local SHERIFF_RELOAD = 2.4 --same as the real revolver
		local SLASH_RANGE = 4.5
		local SLASH_COOLDOWN = 1
		local THROW_MIN, THROW_MAX = 14, 45 -- throw the knife at people this far away
		local THROW_CHANCE = 0.3 --each time it considers throwing(every ~1.5s)
		local THROW_SPEED = 110
		local FLEE_RANGE = 45 --innocents run when the known murderer is this close(and visible)

		--names they show in the server(like real usernames)
		local BOT_NAMES = {
			"xXShadowStrikeXx", "coolkid2011", "Noob_Slayer99", "pizzalover4ever", "Bloxy_Builder", "TheRealJake",
			"SneakyNinja_7", "mrbeanfan22", "Aquatic_Gamer", "ToxicTaco", "GoldenGlider", "Pixel_Panda",
			"RobloxianRex", "Dark_Moonlight", "FrostyFlakes12", "CaptainCookie", "Speedy_Sam", "LunaStarr",
			"BigBadBrick", "Zombie_Hunter_X", "HappyHotdog", "sk8erboi_09", "Mystic_Owl", "TurboTurtle",
	}

	local folder = workspace:FindFirstChild("RoundBots") or Instance.new("Folder")
		folder.Name = "RoundBots"
		folder.Parent = workspace

		local backpacks = game : GetService("ServerStorage") : FindFirstChild("BotBackpacks") or Instance.new("Folder")
		backpacks.Name = "BotBackpacks"
		backpacks.Parent = game : GetService("ServerStorage")

		local rng = Random.new()
		local generation = 0 --bumps every main round so old brains stop(a competition match uses ctx.stopped)
		local allBots = {} --[bot] = true
		local byCharacter = {} --[model] = bot

		------------------------------------------------------------------
		--The bot object(looks like a Player to the round script)
		------------------------------------------------------------------
		local Bot = {}
		Bot.__index = Bot

		function Bot : GetAttribute(name)
		return self._attributes[name]
		end

		function Bot : SetAttribute(name, value)
		self._attributes[name] = value
		if self.Character then
			self.Character : SetAttribute(name, value)
			end
			end

			function Bot : FindFirstChildOfClass(className)
			if className == "Backpack" then
				return self.Backpack
				end
				return nil
				end

				function Bot : IsA(className)
				return className == "Bot"
				end

				function Bot : LoadCharacter() end

				function BotService.isBot(p)
				return type(p) == "table" and p.IsBot == true
				end

				function BotService.fromCharacter(model)
				return byCharacter[model]
				end

				function BotService.characters()
				local list = {}
				for bot in pairs(allBots) do
					if bot.Character then
						table.insert(list, bot.Character)
						end
						end
						return list
						end

						function BotService.countFor(playerCount)
						return BOTS_FOR_PLAYERS[playerCount] or 0
						end

						-- group = nil for the main round, or a competition match's id (names are never shared between groups)
						function BotService.create(count, group)
						local taken = {}
						for bot in pairs(allBots) do
							taken[bot.Name] = true
							end
							local names = {}
							for _, n in ipairs(BOT_NAMES) do
								if not taken[n] then
									table.insert(names, n)
									end
									end
									local list = {}
									for _ = 1, count do
										local name
										if #names > 0 then
											name = table.remove(names, rng:NextInteger(1, #names))
										else
											repeat
											name = BOT_NAMES[rng:NextInteger(1, #BOT_NAMES)] ..rng:NextInteger(10, 99)
											until not taken[name]
											end
											taken[name] = true
											local backpack = Instance.new("Folder")
											backpack.Name = name .. "_Backpack"
											backpack.Parent = backpacks
											local bot = setmetatable({
												IsBot = true,
												Name = name,
												DisplayName = name,
												UserId = 0,
												Parent = Players, -- "still in the server", as far as the round script is concerned
												Backpack = backpack,
												Character = nil,
												group = group,
												_attributes = {},
												}, Bot)
											allBots[bot] = true
											table.insert(list, bot)
											end
											return list
											end

											-- an R6 body like the players' (StarterPlayer.StarterCharacter) with the death ragdoll
											function BotService.spawnCharacter(bot, cframe)
											local template = StarterPlayer:FindFirstChild("StarterCharacter")
											if not template then
												warn("[Bots] StarterPlayer.StarterCharacter is missing")
												return nil
												end
												local char = template : Clone()
												char.Name = bot.Name
												local humanoid = char : FindFirstChildOfClass("Humanoid")
												local root = char : FindFirstChild("HumanoidRootPart")
												if not (humanoid and root) then
													char : Destroy()
													return nil
													end
													char.PrimaryPart = root
													humanoid.DisplayDistanceType = Enum.HumanoidDisplayDistanceType.None
													humanoid.WalkSpeed = 16
													--the same ragdoll players get when they die
													local ragdoll = StarterPlayer:FindFirstChild("StarterCharacterScripts")
													and StarterPlayer.StarterCharacterScripts : FindFirstChild("Script")
													if ragdoll then
														ragdoll : Clone().Parent = char
														end
														char : SetAttribute("IsBot", true)
														char : PivotTo(cframe)
														char.Parent = folder
														for _, part in ipairs(char:GetDescendants()) do
															if part : IsA("BasePart") then
																pcall(function()
																	part:SetNetworkOwner(nil)
																	end)
																end
																end
																bot.Character = char
																byCharacter[char] = bot
																for name, value in pairs(bot._attributes) do
																	char:SetAttribute(name, value)
																	end
																	return char
																	end

																	-- a fresh body for a bot that's already playing (the next round of a competition match): the old one goes
																	function BotService.respawn(bot, cframe)
																	local old = bot.Character
																	if old then
																		byCharacter[old] = nil
																		old : Destroy()
																		end
																		bot.Character = nil
																		return BotService.spawnCharacter(bot, cframe)
																		end

																		------------------------------------------------------------------
																		--Senses
																		------------------------------------------------------------------
																		local function roundActive()
																		local mm = ReplicatedStorage:FindFirstChild("MurderMystery")
																		return mm and mm : GetAttribute("RoundActive") == true
																		end

																		-- can this bot's weapons hurt anyone right now? (a competition match decides for its own bots)
																		local function weaponsLive(bot)
																		local ctx = bot and bot.ctx
																		if ctx and ctx.isLive then
																			return ctx.isLive() == true
																			end
																			return roundActive()
																			end

																			-- competition matches : only the other team of the same match can be hurt(and nobody outside it)
																			local function canHurt(attackerChar, victimChar)
																			if not (attackerChar and victimChar) then
																				return true
																				end
																				local match = attackerChar : GetAttribute("CompMatch")
																				if victimChar : GetAttribute("CompMatch") ~= match then
																					return false
																					end
																					return match == nil or victimChar : GetAttribute("CompTeam") ~= attackerChar : GetAttribute("CompTeam")
																					end

																					local function alive(char)
																					local hum = char and char : FindFirstChildOfClass("Humanoid")
																					return hum ~= nil and hum.Health > 0 and char:FindFirstChild("HumanoidRootPart") ~= nil and char.Parent ~= nil
																					end

																					local function rootOf(char)
																					return char and char : FindFirstChild("HumanoidRootPart")
																					end

																					local function holding(char, toolName)
																					return char and char : FindFirstChild(toolName) ~= nil
																					end

																					local corpses = workspace : FindFirstChild("RoundCorpses")

																					--line of sight from this bot's eyes to another character (walls block, see-through parts don't)
		local function canSee(bot, otherChar, ignoreFacing)
		local char = bot.Character
		local head = char and char:FindFirstChild("Head")
		local root = rootOf(char)
		local otherHead = otherChar and (otherChar : FindFirstChild("Head") or rootOf(otherChar))
		if not (head and root and otherHead) then
			return false
			end
			local offset = otherHead.Position - head.Position
			local dist = offset.Magnitude
			if dist > SIGHT_RANGE then
				return false
				end
				if not ignoreFacing and dist > HEAR_RANGE and root.CFrame.LookVector:Dot(offset.Unit) < FIELD_OF_VIEW then
					return false
					end
					local params = RaycastParams.new()
					params.FilterType = Enum.RaycastFilterType.Exclude
					local ignore = { char }
					if corpses then
						table.insert(ignore, corpses)
						end
						for _ = 1, 6 do
							params.FilterDescendantsInstances = ignore
							local hit = workspace:Raycast(head.Position, offset, params)
							if not hit then
								return true
								end
								if hit.Instance : IsDescendantOf(otherChar) then
									return true
									end
									local part = hit.Instance
									if part : IsA("BasePart") and (part.Transparency > 0.6 or not part.CanCollide) then
										table.insert(ignore, part)
									else
										return false
										end
										end
										return false
										end

										------------------------------------------------------------------
										--Moving around(pathfinding, with a fallback of walking straight there)
										------------------------------------------------------------------
										local AGENT = { AgentRadius = 1.6, AgentHeight = 5, AgentCanJump = true, WaypointSpacing = 5 }

										local function stopMoving(bot)
										bot.goal = nil
										bot.waypoints = nil
										local char = bot.Character
										local hum = char and char:FindFirstChildOfClass("Humanoid")
										local root = rootOf(char)
										if hum and root then
											hum : MoveTo(root.Position)
											end
											end

											local function setGoal(bot, position, direct)
											bot.goal = position
											bot.direct = direct
											end

											local function computePath(bot)
											local root = rootOf(bot.Character)
											if not (root and bot.goal) then
												return
												end
												local path = PathfindingService:CreatePath(AGENT)
												local ok = pcall(function()
													path:ComputeAsync(root.Position, bot.goal)
													end)
												if ok and path.Status == Enum.PathStatus.Success then
													bot.waypoints = path : GetWaypoints()
													bot.waypointIndex = 2
												else
													bot.waypoints = nil -- walk straight at it instead
													-- wedged in a tight spot(between furniture, against a wall) ? try planning from just beside it
													local params = RaycastParams.new()
													params.FilterType = Enum.RaycastFilterType.Exclude
													params.FilterDescendantsInstances = { folder, bot.Character }
													for _, off in ipairs({ Vector3.new(3, 0, 0), Vector3.new(-3, 0, 0), Vector3.new(0, 0, 3), Vector3.new(0, 0, -3) }) do
														if not workspace : Raycast(root.Position, off, params) then
															local retry = PathfindingService : CreatePath(AGENT)
															local ok2 = pcall(function()
																retry:ComputeAsync(root.Position + off, bot.goal)
																end)
															if ok2 and retry.Status == Enum.PathStatus.Success then
																bot.waypoints = retry : GetWaypoints()
																bot.waypointIndex = 1 -- (first step : over to that spot)
																break
																end
																end
																end
																end
																bot.pathGoal = bot.goal
																bot.pathTime = os.clock()
																end

																-- the most open way out from where the bot stands : rays all around at knee / waist / head height,
																--preferring directions with floor ahead(no walking off into nothing) and not straight back into what blocked it.
																--Also says whether a hop is needed(something low in the way, open above it).
																local function openDirection(bot, root, blocked)
																local params = RaycastParams.new()
																params.FilterType = Enum.RaycastFilterType.Exclude
																params.FilterDescendantsInstances = { folder, bot.Character }
																local bestDir, bestScore, bestJump
																local turn = rng:NextNumber(0, math.pi * 2)
																for i = 0, 11 do
																	local a = turn + i * math.pi / 6
																	local dir = Vector3.new(math.cos(a), 0, math.sin(a))
																	local waist = workspace : Raycast(root.Position, dir * 12, params)
																	local head = workspace : Raycast(root.Position + Vector3.new(0, 1.5, 0), dir * 12, params)
																	local knee = workspace : Raycast(root.Position - Vector3.new(0, 1.8, 0), dir * 12, params)
																	local room = math.min(waist and waist.Distance or 12, head and head.Distance or 12)
																	local score = room + rng : NextNumber(0, 2)
																	if not workspace : Raycast(root.Position + dir * math.min(room, 4), Vector3.new(0, -14, 0), params) then
																		score -= 8 --no floor that way
																		end
																		if blocked and dir : Dot(blocked) > 0.75 then
																			score -= 5
																			end
																			if not bestScore or score > bestScore then
																				bestDir, bestScore = dir, score
																				bestJump = knee ~= nil and knee.Distance < 3 and room > 3
																				end
																				end
																				return bestDir, bestJump
																				end

																				-- runs ~10 times a second per bot
																				local function stepMovement(bot)
																				local char = bot.Character
																				local hum = char and char:FindFirstChildOfClass("Humanoid")
																				local root = rootOf(char)
																				if not (hum and root and bot.goal) then
																					return
																					end
																					local flat = Vector3.new(bot.goal.X - root.Position.X, 0, bot.goal.Z - root.Position.Z)
																					if flat.Magnitude < 2.5 then
																						bot.goal = nil
																						bot.waypoints = nil
																						return
																						end
																						-- working its way out of a stuck spot(see below) : head the open way for a moment, then plan again from there
																						if bot.escapeUntil then
																							if os.clock() < bot.escapeUntil then
																								hum : MoveTo(root.Position + bot.escapeDir * 8)
																								if bot.escapeJump then
																									hum.Jump = true
																									end
																									return
																									end
																									bot.escapeUntil = nil
																									bot.pathGoal = nil
																									end
																									if bot.direct then
																										hum : MoveTo(bot.goal)
																									else
																										local needPath = not bot.pathGoal
																										or (bot.pathGoal - bot.goal).Magnitude > 6
																										or os.clock() - (bot.pathTime or 0) > 3
																										if needPath then
																											computePath(bot)
																											end
																											local wps = bot.waypoints
																											if wps and wps[bot.waypointIndex] then
																												local wp = wps[bot.waypointIndex]
																												local d = Vector3.new(wp.Position.X - root.Position.X, 0, wp.Position.Z - root.Position.Z).Magnitude
																												if d < 2.5 then
																													bot.waypointIndex += 1
																													wp = wps[bot.waypointIndex]
																													end
																													if wp then
																														if wp.Action == Enum.PathWaypointAction.Jump then
																															hum.Jump = true
																															end
																															hum : MoveTo(wp.Position)
																														else
																															hum:MoveTo(bot.goal)
																															end
																													else
																														hum:MoveTo(bot.goal)
																														end
																														end
																														-- stuck ? jump, then try another way
																														-- (sideways movement only : jumping on the spot against a wall doesn't count as getting anywhere)
																															local flatPos = Vector3.new(root.Position.X, 0, root.Position.Z)
																															local moved = bot.lastPos and (flatPos - bot.lastPos).Magnitude or 1
																															bot.lastPos = flatPos
																															if moved < 0.15 then
																																bot.stuckFor = (bot.stuckFor or 0) + 0.1
																																if bot.stuckFor > 0.8 then
																																	hum.Jump = true
																																	end
																																else
																																	bot.stuckFor = 0
																																	end
																																	-- also counts as stuck : hardly any real progress for a few seconds(hopping / sliding against a wall)
																																	local now = os.clock()
																																	if not bot.windowAt or now - bot.windowAt >= 1 then
																																		if bot.windowPos and (flatPos - bot.windowPos).Magnitude < 1.5 then
																																			bot.slowFor = (bot.slowFor or 0) + 1
																																		else
																																			bot.slowFor = 0
																																			end
																																			bot.windowPos, bot.windowAt = flatPos, now
																																			end
																																			if bot.stuckFor > 2.5 or bot.slowFor >= 3 then
																																				bot.stuckFor = 0
																																				bot.slowFor = 0
																																				bot.pathGoal = nil
																																				-- how long it's been stuck around here (stuck moments a few seconds apart, within ~10 studs)
																																				if
																																					bot.areaAnchor
																																					and (root.Position - bot.areaAnchor).Magnitude < 10
																																					and now - (bot.areaLast or 0) < 8
																																					then
																																					bot.areaLast = now
																																				else
																																					bot.areaAnchor, bot.areaSince, bot.areaLast = root.Position, now, now
																																					end
																																					-- 1) work its way out : look around for the open way(hop if something low is in the way) and go that way
																																					local blocked = flat.Magnitude > 0 and flat.Unit or nil
																																					bot.escapeDir, bot.escapeJump = openDirection(bot, root, blocked)
																																					bot.escapeUntil = now + rng:NextNumber(0.8, 1.5)
																																					-- 2) still stuck in the same area after a few tries(goal behind a wall, outside the building) :
																																					--give up on that goal so the brain picks something else
																																					if bot.nudgeAnchor and (root.Position - bot.nudgeAnchor).Magnitude < 8 then
																																						bot.nudges = (bot.nudges or 0) + 1
																																					else
																																						bot.nudgeAnchor = root.Position
																																						bot.nudges = 1
																																						end
																																						if bot.nudges >= 3 then
																																							bot.nudges = 0
																																							bot.nudgeAnchor = nil
																																							bot.goal = nil
																																							bot.waypoints = nil
																																							return
																																							end
																																							-- 3) last resort, stuck in one spot for ages(fell into a closed - off gap) : back to a spawn on the map
																																							local parts = bot.ctx and bot.ctx.spawnParts
																																							if now - bot.areaSince > 16 and parts and #parts > 0 then
																																								local spawnPart = parts[rng:NextInteger(1, #parts)]
																																								root.CFrame = spawnPart.CFrame * CFrame.new(0, spawnPart.Size.Y / 2 + 3, 0)
																																								root.AssemblyLinearVelocity = Vector3.zero
																																								bot.areaAnchor, bot.escapeUntil, bot.goal, bot.waypoints = nil, nil, nil, nil
																																								return
																																								end
																																								bot.goal = bot.goal + Vector3.new(rng:NextNumber(-6, 6), 0, rng : NextNumber(-6, 6))
																																								end
																																								end

																																								-- a random spot on the floor near one of the map's spawns
																																								local function randomSpot(ctx)
																																								local parts = ctx.spawnParts
																																								if not parts or #parts == 0 then
																																									return nil
																																									end
																																									local params = RaycastParams.new()
																																									params.FilterType = Enum.RaycastFilterType.Exclude
																																									params.FilterDescendantsInstances = { folder }
																																									local function roofed(pos)
																																									return workspace:Raycast(pos, Vector3.new(0, 40, 0), params) ~= nil
																																									end
																																									local loose -- first floor hit, used if no spot passes the checks below
																																									for _ = 1, 8 do
																																										local base = parts[rng:NextInteger(1, #parts)]
																																										local guess = base.Position + Vector3.new(rng : NextNumber(-18, 18), 6, rng : NextNumber(-18, 18))
																																										local hit = workspace:Raycast(guess, Vector3.new(0, -30, 0), params)
																																										if hit and hit.Instance.CanCollide then
																																											local spot = hit.Position + Vector3.new(0, 3, 0)
																																											loose = loose or spot
																																											-- same floor as the spawn(not the ground outside / below the building), and indoors if the spawn is
																																											-- indoors(not the other side of an outer wall), so the spot can actually be walked to
																																											if
																																												hit.Position.Y >= base.Position.Y - 5
																																												and (not roofed(base.Position + Vector3.new(0, 3, 0)) or roofed(spot))
																																												then
																																												return spot
																																												end
																																												end
																																												end
																																												return loose or parts[rng:NextInteger(1, #parts)].Position + Vector3.new(0, 3, 0)
																																												end

																																												-- the spot(out of a few random ones) furthest from a danger
																																												local function escapeSpot(ctx, from, danger)
																																												local best, bestScore
																																												for _ = 1, 8 do
																																													local spot = randomSpot(ctx)
																																													if spot then
																																														local score = (spot - danger).Magnitude - (spot - from).Magnitude * 0.3
																																														if not bestScore or score > bestScore then
																																															best, bestScore = spot, score
																																															end
																																															end
																																															end
																																															return best
																																															end

																																															local function face(bot, position)
																																															local root = rootOf(bot.Character)
																																															if root then
																																																local flat = Vector3.new(position.X, root.Position.Y, position.Z)
																																																if (flat - root.Position).Magnitude > 0.1 then
																																																	root.CFrame = CFrame.lookAt(root.Position, flat)
																																																	end
																																																	end
																																																	end

																																																	------------------------------------------------------------------
																																																	--Weapons
																																																	------------------------------------------------------------------
																																																	local function findTool(bot, name)
																																																	local char = bot.Character
																																																	return (char and char:FindFirstChild(name)) or bot.Backpack:FindFirstChild(name)
																																																	end

																																																	local function equip(bot, name)
																																																	local char = bot.Character
																																																	local hum = char and char : FindFirstChildOfClass("Humanoid")
																																																	local tool = findTool(bot, name)
																																																	if hum and tool and tool.Parent ~= char then
																																																		for _, t in ipairs(char:GetChildren()) do
																																																			if t : IsA("Tool") then
																																																				t.Parent = bot.Backpack
																																																				end
																																																				end
																																																				hum : EquipTool(tool)
																																																				end
																																																				return tool
																																																				end

																																																				local function unequip(bot)
																																																				local char = bot.Character
																																																				if char then
																																																					for _, t in ipairs(char:GetChildren()) do
																																																						if t:IsA("Tool") then
																																																							t.Parent = bot.Backpack
																																																							end
																																																							end
																																																							end
																																																							end

																																																							local function playToolSound(tool, soundName, parent)
																																																							local handle = tool and tool:FindFirstChild("Handle")
																																																							local sound = handle and handle : FindFirstChild(soundName)
																																																							if sound and sound : IsA("Sound") then
																																																								if parent then
																																																									local copy = sound : Clone()
																																																									copy.Parent = parent
																																																									copy : Play()
																																																									Debris : AddItem(copy, 3)
																																																								else
																																																									sound:Play()
																																																									end
																																																									end
																																																									end

																																																									local function killCharacter(victimChar, tool, bot)
																																																									if not weaponsLive(bot) or not canHurt(bot and bot.Character, victimChar) then
																																																										return
																																																										end
																																																										local hum = victimChar:FindFirstChildOfClass("Humanoid")
																																																										if not (hum and hum.Health > 0) then
																																																											return
																																																											end
																																																											local head = victimChar : FindFirstChild("Head")
																																																											if head then
																																																												playToolSound(tool, "Kill", head)
																																																												end
																																																												hum : TakeDamage(100)
																																																												end

																																																												-- the murderer bot throws its knife(plain blade, spins, flies straight; sticks in walls)
																																																												local function throwKnife(bot, targetChar)
																																																												local char = bot.Character
																																																												local head = char and char:FindFirstChild("Head")
																																																												local tool = findTool(bot, KNIFE_NAME)
																																																												local handle = tool and tool : FindFirstChild("Handle")
																																																												local targetRoot = rootOf(targetChar)
																																																												if not (head and handle and targetRoot) then
																																																													return
																																																													end
																																																													-- aim where they're heading, with a human amount of error
																																																													local lead = targetRoot.AssemblyLinearVelocity * ((targetRoot.Position - head.Position).Magnitude / THROW_SPEED)
																																																													local aimPoint = targetRoot.Position + lead * rng:NextNumber(0.4, 1)
																																																													+ Vector3.new(rng : NextNumber(-2, 2), rng : NextNumber(-1.5, 1.5), rng : NextNumber(-2, 2))
																																																													local direction = (aimPoint - head.Position).Unit
																																																													local knife = handle : Clone()
																																																													for _, d in ipairs(knife : GetDescendants()) do
																																																														if not (d : IsA("SpecialMesh") or d : IsA("Sound")) then
																																																															d : Destroy()
																																																															end
																																																															end
																																																															knife.Name = "BotThrownKnife"
																																																															knife.Anchored = true
																																																															knife.CanCollide = false
																																																															knife.CanQuery = false
																																																															knife.CanTouch = false
																																																															knife.Transparency = 0
																																																															local pos = head.Position + direction * 2
																																																															knife.CFrame = CFrame.lookAt(pos, pos + direction) * CFrame.Angles(-math.pi / 2, 0, 0)
																																																															knife.Parent = workspace
																																																															playToolSound(tool, "Throw")
																																																															handle.Transparency = 1
																																																															task.delay(1, function()
																																																																if handle.Parent then
																																																																	handle.Transparency = 0
																																																																	playToolSound(tool, "Out")
																																																																	end
																																																																	end)
																																																															local params = RaycastParams.new()
																																																															params.FilterType = Enum.RaycastFilterType.Exclude
																																																															local ignore = { char, knife }
																																																															if corpses then
																																																																table.insert(ignore, corpses)
																																																																end
																																																																task.spawn(function()
																																																																	local spin = 0
																																																																	local t0 = os.clock()
																																																																	while knife.Parent and os.clock() - t0 < 3 do
																																																																		local dt = RunService.Heartbeat:Wait()
																																																																		local step = direction * THROW_SPEED * dt
																																																																		params.FilterDescendantsInstances = ignore
																																																																		local hit = workspace : Raycast(pos, step, params)
																																																																		if hit then
																																																																			local model = hit.Instance : FindFirstAncestorOfClass("Model")
																																																																			local hum = model and model : FindFirstChildOfClass("Humanoid")
																																																																			if hum and hum.Health > 0 and canHurt(char, model) then
																																																																				killCharacter(model, tool, bot)
																																																																				knife:Destroy()
																																																																				return
																																																																				elseif hit.Instance : IsA("BasePart") and (hit.Instance.Transparency > 0.6 or not hit.Instance.CanCollide) then
																																																																				table.insert(ignore, hit.Instance)
																																																																			else
																																																																				knife.CFrame = CFrame.lookAt(hit.Position, hit.Position + direction) * CFrame.Angles(-math.pi / 2, 0, 0)
																																																																				local stick = knife:FindFirstChild("Stick")
																																																																				if stick and stick : IsA("Sound") then
																																																																					stick : Play()
																																																																					end
																																																																					Debris : AddItem(knife, 6)
																																																																					return
																																																																					end
																																																																					end
																																																																					pos += step
																																																																					spin += dt * 30
																																																																					knife.CFrame = CFrame.lookAt(pos, pos + direction) * CFrame.Angles(-math.pi / 2 + spin, 0, 0)
																																																																					end
																																																																					knife : Destroy()
																																																																					end)
																																																																end

																																																																-- the sheriff bot fires(same rules as the real revolver : an innocent hit kills the shooter too)
																																																																local function shoot(bot, targetChar)
																																																																local char = bot.Character
																																																																local head = char and char:FindFirstChild("Head")
																																																																local tool = findTool(bot, GUN_NAME)
																																																																local targetRoot = rootOf(targetChar)
																																																																if not (head and tool and targetRoot) then
																																																																	return
																																																																	end
																																																																	playToolSound(tool, "FireSound")
																																																																	task.delay(0.95, function()
																																																																		if tool.Parent == char then
																																																																			playToolSound(tool, "Reload")
																																																																			end
																																																																			end)
																																																																	local dist = (targetRoot.Position - head.Position).Magnitude
																																																																	local hitChance = math.clamp(0.75 - dist / 100, 0.2, 0.7)
																																																																	local aimPoint = targetRoot.Position + Vector3.new(0, 0.6, 0)
																																																																	if rng:NextNumber() > hitChance then
																																																																		-- a miss : off to one side
																																																																		local side = (aimPoint - head.Position) : Cross(Vector3.yAxis)
																																																																		side = side.Magnitude > 0 and side.Unit or Vector3.xAxis
																																																																		aimPoint += side * rng:NextNumber(2.5, 5) * (rng : NextNumber() < 0.5 and -1 or 1)
																																																																		+ Vector3.new(0, rng:NextNumber(-1, 2), 0)
																																																																	else
																																																																		aimPoint += Vector3.new(rng:NextNumber(-0.4, 0.4), rng : NextNumber(-0.4, 0.4), rng : NextNumber(-0.4, 0.4))
																																																																		end
																																																																		local params = RaycastParams.new()
																																																																		params.FilterType = Enum.RaycastFilterType.Exclude
																																																																		local ignore = { char }
																																																																		if corpses then
																																																																			table.insert(ignore, corpses)
																																																																			end
																																																																			local direction = (aimPoint - head.Position).Unit * 200
																																																																			for _ = 1, 8 do
																																																																				params.FilterDescendantsInstances = ignore
																																																																				local hit = workspace:Raycast(head.Position, direction, params)
																																																																				if not hit then
																																																																					return
																																																																					end
																																																																					local model = hit.Instance : FindFirstAncestorOfClass("Model")
																																																																					local hum = model and model : FindFirstChildOfClass("Humanoid")
																																																																					if hum and hum.Health > 0 and model ~= char then
																																																																						if not weaponsLive(bot) or not canHurt(char, model) then
																																																																							return
																																																																							end
																																																																							if hum:GetAttribute("Bulletproof") then
																																																																								hum : SetAttribute("Bulletproof", nil)
																																																																								return
																																																																								end
																																																																								-- (no penalty in a competition match : the other team are all fair targets)
																																																																								local wasInnocent = hum : FindFirstChild("Murderer") == nil and not char : GetAttribute("CompMatch")
																																																																								hum : TakeDamage(100)
																																																																								if wasInnocent then
																																																																									local myHum = char : FindFirstChildOfClass("Humanoid")
																																																																									if myHum then
																																																																										myHum : TakeDamage(100)
																																																																										end
																																																																										end
																																																																										return
																																																																										end
																																																																										if hit.Instance : IsA("BasePart") and hit.Instance.Transparency >= 1 then
																																																																											table.insert(ignore, hit.Instance)
																																																																										else
																																																																											return --hit a wall
																																																																											end
																																																																											end
																																																																											end

																																																																											------------------------------------------------------------------
																																																																											--Brains
																																																																											------------------------------------------------------------------
																																																																											local function others(ctx, bot)
																																																																											local list = {}
																																																																											for _, p in ipairs(ctx.participants) do
																																																																												-- (a competition match : only the other team)
																																																																												if p ~= bot and ctx.inRound(p) and p.Character and not (ctx.teams and ctx.teams[p] == ctx.teams[bot]) then
																																																																													table.insert(list, p)
																																																																													end
																																																																													end
																																																																													return list
																																																																													end

																																																																													local function murdererChar(ctx)
																																																																													local m = ctx.murderer
																																																																													return m and ctx.inRound(m) and m.Character or nil
																																																																													end

																																																																													-- everyone who can see the murderer right now learns who they are(used when a kill happens)
																																																																													local function witnessKill(ctx, victimChar)
																																																																													local mChar = murdererChar(ctx)
																																																																													local mRoot = rootOf(mChar)
																																																																													local vRoot = rootOf(victimChar)
																																																																													if not (mRoot and vRoot) then
																																																																														return
																																																																														end
																																																																														-- only obvious for a close - up kill(a thrown knife from far away is much harder to spot)
																																																																														if (mRoot.Position - vRoot.Position).Magnitude > 14 then
																																																																															return
																																																																															end
																																																																															for bot in pairs(allBots) do
																																																																																local root = rootOf(bot.Character)
																																																																																local near = root and (root.Position - mRoot.Position).Magnitude < 45
																																																																																--people nearby notice a kill only some of the time(they might be looking at something else)
																																																																																if bot.ctx == ctx and bot ~= ctx.murderer and near and alive(bot.Character)
																																																																																	and rng:NextNumber() < 0.55 and canSee(bot, mChar, true) then
																																																																																	bot.knowsMurderer = true
																																																																																	bot.lastSeenMurderer = mRoot.Position
																																																																																	bot.noticedAt = os.clock()
																																																																																	end
																																																																																	end
																																																																																	end

																																																																																	-- idle wandering, like someone exploring the map
																																																																																	local function wander(bot, ctx)
																																																																																	local now = os.clock()
																																																																																	if bot.idleUntil and now < bot.idleUntil then
																																																																																		if rng:NextNumber() < 0.04 then
																																																																																			face(bot, rootOf(bot.Character).Position + Vector3.new(rng:NextNumber(-1, 1), 0, rng : NextNumber(-1, 1)))
																																																																																			end
																																																																																			return
																																																																																			end
																																																																																			if not bot.goal then
																																																																																				if rng:NextNumber() < 0.35 then
																																																																																					bot.idleUntil = now + rng : NextNumber(1, 4) --stop and look around for a bit
																																																																																					stopMoving(bot)
																																																																																				else
																																																																																					local spot = randomSpot(ctx)
																																																																																					if spot then
																																																																																						setGoal(bot, spot)
																																																																																						end
																																																																																						end
																																																																																						end
																																																																																						if rng:NextNumber() < 0.006 then
																																																																																							local hum = bot.Character : FindFirstChildOfClass("Humanoid")
																																																																																							if hum then
																																																																																								hum.Jump = true --the occasional jump, like a bored player
																																																																																								end
																																																																																								end
																																																																																								end

																																																																																								-- did I just see the murderer with the knife out ?
																																																																																								local function lookForKnife(bot, ctx)
																																																																																								local mChar = murdererChar(ctx)
																																																																																								if mChar and holding(mChar, KNIFE_NAME) and canSee(bot, mChar) then
																																																																																									if not bot.knowsMurderer then
																																																																																										bot.noticedAt = os.clock()
																																																																																										end
																																																																																										bot.knowsMurderer = true
																																																																																										bot.lastSeenMurderer = rootOf(mChar).Position
																																																																																										return mChar
																																																																																										end
																																																																																										if bot.knowsMurderer and mChar and canSee(bot, mChar) then
																																																																																											bot.lastSeenMurderer = rootOf(mChar).Position
																																																																																											return mChar
																																																																																											end
																																																																																											return nil
																																																																																											end

																																																																																											local function innocentThink(bot, ctx)
																																																																																											local char = bot.Character
																																																																																											local root = rootOf(char)
																																																																																											local seen = lookForKnife(bot, ctx)
																																																																																											local now = os.clock()
																																																																																											--run from the murderer(after a human reaction delay)
																																																																																											if seen and bot.knowsMurderer and now - (bot.noticedAt or 0) > bot.reaction then
																																																																																												local mRoot = rootOf(seen)
																																																																																												if mRoot and (mRoot.Position - root.Position).Magnitude < FLEE_RANGE then
																																																																																													if not bot.fleeing or not bot.goal or now > bot.fleeing then
																																																																																														local spot = escapeSpot(ctx, root.Position, mRoot.Position)
																																																																																														if spot then
																																																																																															setGoal(bot, spot)
																																																																																															end
																																																																																															bot.fleeing = now + 2.5
																																																																																															bot.idleUntil = nil
																																																																																															end
																																																																																															return
																																																																																															end
																																																																																															end
																																																																																															-- a dropped gun nearby : go and get it, but like a person, not instantly : each bot only
																																																																																															-- "notices" it 6 - 14 seconds after it's within 50 studs, so real players get a fair chance to
																																																																																															--see it lying there and grab it first
																																																																																															local gun = workspace : FindFirstChild("DroppedGun")
																																																																																															if gun and gun : IsA("Model") then
																																																																																																gun = gun.PrimaryPart-- (the dropped gun is a model : its pickup part)
																																																																																																end
																																																																																																if gun and gun : IsA("BasePart") and (gun.Position - root.Position).Magnitude < 50 then
																																																																																																	if bot.gunSeen ~= gun then
																																																																																																		bot.gunSeen = gun
																																																																																																		bot.gunNoticeAt = now + rng : NextNumber(6, 14)
																																																																																																		end
																																																																																																		if now >= bot.gunNoticeAt then
																																																																																																			bot.idleUntil = nil
																																																																																																			setGoal(bot, gun.Position)
																																																																																																			return
																																																																																																			end
																																																																																																			end
																																																																																																			if bot.fleeing and now < bot.fleeing then
																																																																																																				return
																																																																																																				end
																																																																																																				bot.fleeing = nil
																																																																																																				wander(bot, ctx)
																																																																																																				end

																																																																																																				local function sheriffThink(bot, ctx)
																																																																																																				local char = bot.Character
																																																																																																				local root = rootOf(char)
																																																																																																				local now = os.clock()
																																																																																																				local seen = lookForKnife(bot, ctx)
																																																																																																				if seen and bot.knowsMurderer then
																																																																																																					local mRoot = rootOf(seen)
																																																																																																					local dist = (mRoot.Position - root.Position).Magnitude
																																																																																																					if dist < SHERIFF_RANGE then
																																																																																																						-- stop, face them, gun out; shoot once the reaction time has passed
																																																																																																						stopMoving(bot)
																																																																																																						bot.idleUntil = nil
																																																																																																						equip(bot, GUN_NAME)
																																																																																																						face(bot, mRoot.Position)
																																																																																																						if dist > 60 then
																																																																																																							setGoal(bot, mRoot.Position) --too far for a good shot : get closer
																																																																																																							end
																																																																																																							if now - (bot.noticedAt or 0) > bot.reaction + 0.5 and now >= (bot.gunReadyAt or 0) then
																																																																																																								bot.gunReadyAt = now + SHERIFF_RELOAD
																																																																																																								shoot(bot, seen)
																																																																																																								end
																																																																																																								return
																																																																																																								end
																																																																																																								end
																																																																																																								if bot.knowsMurderer and bot.lastSeenMurderer then
																																																																																																									-- hunt: go to where they were last seen
																																																																																																									equip(bot, GUN_NAME)
																																																																																																									bot.idleUntil = nil
																																																																																																									setGoal(bot, bot.lastSeenMurderer)
																																																																																																									if (bot.lastSeenMurderer - root.Position).Magnitude < 6 then
																																																																																																										bot.lastSeenMurderer = nil
																																																																																																										end
																																																																																																										return
																																																																																																										end
																																																																																																										-- nothing seen yet : wander, sometimes with the gun out
																																																																																																										if not bot.gunHabit then
																																																																																																											bot.gunHabit = rng : NextNumber() < 0.5 and "out" or "away"
																																																																																																											end
																																																																																																											if bot.gunHabit == "out" then
																																																																																																												equip(bot, GUN_NAME)
																																																																																																											else
																																																																																																												unequip(bot)
																																																																																																												end
																																																																																																												wander(bot, ctx)
																																																																																																												end

																																																																																																												-- free - for - all modes : nobody in sight->go to where someone was last seen, then after a while
																																																																																																												--go looking for whoever is closest(like hearing them), so the last few always find each other
																																																																																																												local function huntSomeone(bot, ctx, root, now)
																																																																																																												if bot.lastSeenEnemy then
																																																																																																													local flat = Vector3.new(bot.lastSeenEnemy.X - root.Position.X, 0, bot.lastSeenEnemy.Z - root.Position.Z)
																																																																																																													if flat.Magnitude < 6 or now - (bot.lastSeenAt or 0) > 12 then
																																																																																																														bot.lastSeenEnemy = nil -- got there(or gave up) : nobody here
																																																																																																													else
																																																																																																														setGoal(bot, bot.lastSeenEnemy)
																																																																																																														return
																																																																																																														end
																																																																																																														end
																																																																																																														if now - (bot.lastSeenAt or 0) > 8 and now >= (bot.huntCheck or 0) then
																																																																																																															bot.huntCheck = now + 3
																																																																																																															local nearest, best
																																																																																																															for _, p in ipairs(others(ctx, bot)) do
																																																																																																																local r = rootOf(p.Character)
																																																																																																																local d = r and (r.Position - root.Position).Magnitude
																																																																																																																if d and (not best or d < best) then
																																																																																																																	nearest, best = r.Position, d
																																																																																																																	end
																																																																																																																	end
																																																																																																																	if nearest then
																																																																																																																		setGoal(bot, nearest)
																																																																																																																		return
																																																																																																																		end
																																																																																																																		end
																																																																																																																		if not bot.goal then
																																																																																																																			wander(bot, ctx)
																																																																																																																			end
																																																																																																																			end

																																																																																																																			-- "All Murderers" mode: everyone has a knife, last one standing wins.
																																																																																																																			--No sneaking here : go for the closest person in sight, fight back when someone gets close,
																																																																																																																			-- throw the knife from range now and then, and hunt where someone was last seen.
																																																																																																																			local function knifeFightThink(bot, ctx)
																																																																																																																			local root = rootOf(bot.Character)
																																																																																																																			local now = os.clock()
																																																																																																																			local knife = findTool(bot, KNIFE_NAME)
																																																																																																																			if not knife or not weaponsLive(bot) then
																																																																																																																				wander(bot, ctx) -- (the knives come out a few seconds after the start)
																																																																																																																				return
																																																																																																																				end
																																																																																																																				-- target: the closest person I can see(anyone right next to me always counts)
																																																																																																																				local target, best
																																																																																																																				for _, p in ipairs(others(ctx, bot)) do
																																																																																																																					local r = rootOf(p.Character)
																																																																																																																					if r then
																																																																																																																						local d = (r.Position - root.Position).Magnitude
																																																																																																																						if (d < 10 or (d < SIGHT_RANGE and canSee(bot, p.Character))) and (not best or d < best) then
																																																																																																																							target, best = p, d
																																																																																																																							end
																																																																																																																							end
																																																																																																																							end
																																																																																																																							equip(bot, KNIFE_NAME)
																																																																																																																							if not target then
																																																																																																																								bot.fightTarget = nil
																																																																																																																								huntSomeone(bot, ctx, root, now)
																																																																																																																								return
																																																																																																																								end
																																																																																																																								if bot.fightTarget ~= target then
																																																																																																																									bot.fightTarget = target
																																																																																																																									bot.noticedAt = now
																																																																																																																									end
																																																																																																																									local tChar = target.Character
																																																																																																																									local tRoot = rootOf(tChar)
																																																																																																																									bot.lastSeenEnemy = tRoot.Position
																																																																																																																									bot.lastSeenAt = now
																																																																																																																									bot.idleUntil = nil
																																																																																																																									if now - (bot.noticedAt or 0) < bot.reaction then
																																																																																																																										face(bot, tRoot.Position) --a moment to react
																																																																																																																										return
																																																																																																																										end
																																																																																																																										-- close in, aiming a little ahead of where they're going
																																																																																																																										local vel = tRoot.AssemblyLinearVelocity
																																																																																																																										setGoal(bot, tRoot.Position + Vector3.new(vel.X, 0, vel.Z) * math.clamp(best / 40, 0, 0.6), true)
																																																																																																																										if best <= SLASH_RANGE and now >= (bot.slashReadyAt or 0) then
																																																																																																																											bot.slashReadyAt = now + SLASH_COOLDOWN
																																																																																																																											face(bot, tRoot.Position)
																																																																																																																											playToolSound(knife, "Swoosh")
																																																																																																																											task.delay(0.25, function()
																																																																																																																												local r = rootOf(bot.Character)
																																																																																																																												local vr = rootOf(tChar)
																																																																																																																												if r and vr and (vr.Position - r.Position).Magnitude <= SLASH_RANGE + 1.5 and alive(bot.Character) then
																																																																																																																													killCharacter(tChar, knife, bot)
																																																																																																																													end
																																																																																																																													end)
																																																																																																																											elseif best > THROW_MIN and best < THROW_MAX and now >= (bot.nextThrowCheck or 0) then
																																																																																																																											bot.nextThrowCheck = now + 1.5
																																																																																																																											if rng:NextNumber() < THROW_CHANCE + 0.1 and now >= (bot.slashReadyAt or 0) and canSee(bot, tChar) then
																																																																																																																												face(bot, tRoot.Position)
																																																																																																																												bot.slashReadyAt = now + 1.2
																																																																																																																												throwKnife(bot, tChar)
																																																																																																																												end
																																																																																																																												end
																																																																																																																												end

																																																																																																																												-- would a shot from my head reach their body ? (not blocked by a ledge, railing or wall)
																																																																																																																												local function clearShot(bot, targetChar)
																																																																																																																												local head = bot.Character and bot.Character : FindFirstChild("Head")
																																																																																																																												local tRoot = rootOf(targetChar)
																																																																																																																												if not (head and tRoot) then
																																																																																																																													return false
																																																																																																																													end
																																																																																																																													local params = RaycastParams.new()
																																																																																																																													params.FilterType = Enum.RaycastFilterType.Exclude
																																																																																																																													local ignore = { bot.Character }
																																																																																																																													local corpses = workspace:FindFirstChild("RoundCorpses")
																																																																																																																													if corpses then
																																																																																																																														table.insert(ignore, corpses)
																																																																																																																														end
																																																																																																																														local aim = tRoot.Position + Vector3.new(0, 0.6, 0) - head.Position
																																																																																																																														for _ = 1, 6 do
																																																																																																																															params.FilterDescendantsInstances = ignore
																																																																																																																															local hit = workspace:Raycast(head.Position, aim * 1.05, params)
																																																																																																																															if not hit then
																																																																																																																																return false
																																																																																																																																end
																																																																																																																																if hit.Instance : IsDescendantOf(targetChar) then
																																																																																																																																	return true
																																																																																																																																	end
																																																																																																																																	if hit.Instance : IsA("BasePart") and hit.Instance.Transparency >= 1 then
																																																																																																																																		table.insert(ignore, hit.Instance) -- (the revolver shoots through invisible parts too)
																																																																																																																																	else
																																																																																																																																		return false
																																																																																																																																		end
																																																																																																																																		end
																																																																																																																																		return false
																																																																																																																																		end

																																																																																																																																		-- "Sheriffs Only" mode: everyone has a gun, last one standing wins
																																																																																																																																		local function gunfightThink(bot, ctx)
																																																																																																																																		local root = rootOf(bot.Character)
																																																																																																																																		local now = os.clock()
																																																																																																																																		if not findTool(bot, GUN_NAME) then
																																																																																																																																			wander(bot, ctx)
																																																																																																																																			return
																																																																																																																																			end
																																																																																																																																			-- the closest person I can see
																																																																																																																																			local target, best
																																																																																																																																			for _, p in ipairs(others(ctx, bot)) do
																																																																																																																																				local r = rootOf(p.Character)
																																																																																																																																				if r then
																																																																																																																																					local d = (r.Position - root.Position).Magnitude
																																																																																																																																					if d < SHERIFF_RANGE and (not best or d < best) and canSee(bot, p.Character) then
																																																																																																																																						target, best = p, d
																																																																																																																																						end
																																																																																																																																						end
																																																																																																																																						end
																																																																																																																																						if target then
																																																																																																																																							if bot.fightTarget ~= target then
																																																																																																																																								bot.fightTarget = target
																																																																																																																																								bot.noticedAt = now
																																																																																																																																								end
																																																																																																																																								local tRoot = rootOf(target.Character)
																																																																																																																																								bot.lastSeenEnemy = tRoot.Position
																																																																																																																																								bot.lastSeenAt = now
																																																																																																																																								bot.idleUntil = nil
																																																																																																																																								equip(bot, GUN_NAME)
																																																																																																																																								face(bot, tRoot.Position)
																																																																																																																																								local clear = clearShot(bot, target.Character)
																																																																																																																																								if not clear then
																																																																																																																																									setGoal(bot, tRoot.Position) --something's in the way (ledge, railing): move for a better angle
																																																																																																																																									elseif best > 55 then
																																																																																																																																									setGoal(bot, tRoot.Position) --too far for a good shot : get closer
																																																																																																																																									elseif best < 12 then
																																																																																																																																									-- too close : back off a bit(and a little sideways)
																																																																																																																																									local away = Vector3.new(root.Position.X - tRoot.Position.X, 0, root.Position.Z - tRoot.Position.Z)
																																																																																																																																									if away.Magnitude > 0.1 then
																																																																																																																																										setGoal(bot, root.Position + away.Unit * 8 + root.CFrame.RightVector * rng:NextNumber(-4, 4), true)
																																																																																																																																										end
																																																																																																																																									else
																																																																																																																																										stopMoving(bot)
																																																																																																																																										end
																																																																																																																																										if clear and now - (bot.noticedAt or 0) > bot.reaction + 0.4 and now >= (bot.gunReadyAt or 0) then
																																																																																																																																											bot.gunReadyAt = now + SHERIFF_RELOAD
																																																																																																																																											face(bot, tRoot.Position)
																																																																																																																																											shoot(bot, target.Character)
																																																																																																																																											end
																																																																																																																																											return
																																																																																																																																											end
																																																																																																																																											bot.fightTarget = nil
																																																																																																																																											-- nobody in sight : hunt for someone with the gun out
																																																																																																																																											equip(bot, GUN_NAME)
																																																																																																																																											huntSomeone(bot, ctx, root, now)
																																																																																																																																											end

																																																																																																																																											-- could anyone holding a gun see me right now ? (a careful murderer keeps the knife hidden then)
																																																																																																																																											local function gunWatching(bot, ctx)
																																																																																																																																											local myHead = bot.Character and bot.Character : FindFirstChild("Head")
																																																																																																																																											if not myHead then
																																																																																																																																												return false
																																																																																																																																												end
																																																																																																																																												for _, p in ipairs(others(ctx, bot)) do
																																																																																																																																													local c = p.Character
																																																																																																																																													local head = c and c:FindFirstChild("Head")
																																																																																																																																													local hasGun = c and (c : FindFirstChild(GUN_NAME) or (p : FindFirstChildOfClass("Backpack") and p : FindFirstChildOfClass("Backpack") : FindFirstChild(GUN_NAME)))
																																																																																																																																													if head and hasGun and (head.Position - myHead.Position).Magnitude < 70 then
																																																																																																																																														local params = RaycastParams.new()
																																																																																																																																														params.FilterType = Enum.RaycastFilterType.Exclude
																																																																																																																																														params.FilterDescendantsInstances = { c, corpses }
																																																																																																																																														local hit = workspace:Raycast(head.Position, myHead.Position - head.Position, params)
																																																																																																																																														if hit and hit.Instance : IsDescendantOf(bot.Character) then
																																																																																																																																															return true
																																																																																																																																															end
																																																																																																																																															end
																																																																																																																																															end
																																																																																																																																															return false
																																																																																																																																															end

																																																																																																																																															local function pickVictim(bot, ctx)
																																																																																																																																															local root = rootOf(bot.Character)
																																																																																																																																															local list = others(ctx, bot)
																																																																																																																																															if #list == 0 then
																																																																																																																																																return nil
																																																																																																																																																end
																																																																																																																																																-- prefer people who are near and on their own(fewest other people around them)
																																																																																																																																																local best, bestScore
																																																																																																																																																for _, p in ipairs(list) do
																																																																																																																																																	local r = rootOf(p.Character)
																																																																																																																																																	if r then
																																																																																																																																																		local crowd = 0
																																																																																																																																																		for _, q in ipairs(list) do
																																																																																																																																																			local qr = q ~= p and rootOf(q.Character)
																																																																																																																																																			if qr and (qr.Position - r.Position).Magnitude < 20 then
																																																																																																																																																				crowd += 1
																																																																																																																																																				end
																																																																																																																																																				end
																																																																																																																																																				local score = (r.Position - root.Position).Magnitude + crowd * 25 + rng:NextNumber(0, 30)
																																																																																																																																																				if not bestScore or score < bestScore then
																																																																																																																																																					best, bestScore = p, score
																																																																																																																																																					end
																																																																																																																																																					end
																																																																																																																																																					end
																																																																																																																																																					return best
																																																																																																																																																					end

																																																																																																																																																					local function murdererThink(bot, ctx)
																																																																																																																																																					local char = bot.Character
																																																																																																																																																					local root = rootOf(char)
																																																																																																																																																					local now = os.clock()
																																																																																																																																																					local hasKnife = findTool(bot, KNIFE_NAME) ~= nil
																																																																																																																																																					if not hasKnife or not roundActive() then
																																																																																																																																																						unequip(bot)
																																																																																																																																																						wander(bot, ctx)
																																																																																																																																																						return
																																																																																																																																																						end
																																																																																																																																																						if not bot.huntAfter then
																																																																																																																																																							bot.huntAfter = now + rng:NextNumber(2, 6) --act normal for a moment after getting the knife
																																																																																																																																																							end
																																																																																																																																																							-- lying low after a kill
																																																																																																																																																							if bot.layLowUntil and now < bot.layLowUntil then
																																																																																																																																																								if not bot.goal then
																																																																																																																																																									wander(bot, ctx)
																																																																																																																																																									end
																																																																																																																																																									return
																																																																																																																																																									end
																																																																																																																																																									if now < bot.huntAfter then
																																																																																																																																																										wander(bot, ctx)
																																																																																																																																																										return
																																																																																																																																																										end
																																																																																																																																																										-- a sheriff pointing a gun at me : rush them or get away
																																																																																																																																																										for _, p in ipairs(others(ctx, bot)) do
																																																																																																																																																											local c = p.Character
																																																																																																																																																											if holding(c, GUN_NAME) and canSee(bot, c) then
																																																																																																																																																												local r = rootOf(c)
																																																																																																																																																												if r and (r.Position - root.Position).Magnitude < 40 then
																																																																																																																																																													if not bot.gunReaction then
																																																																																																																																																														bot.gunReaction = rng:NextNumber() < 0.5 and "rush" or "run"
																																																																																																																																																														end
																																																																																																																																																														if bot.gunReaction == "rush" then
																																																																																																																																																															bot.victim = p
																																																																																																																																																														else
																																																																																																																																																															local spot = escapeSpot(ctx, root.Position, r.Position)
																																																																																																																																																															if spot then
																																																																																																																																																																setGoal(bot, spot)
																																																																																																																																																																end
																																																																																																																																																																unequip(bot)
																																																																																																																																																																bot.layLowUntil = now + rng:NextNumber(3, 6)
																																																																																																																																																																bot.gunReaction = nil
																																																																																																																																																																return
																																																																																																																																																																end
																																																																																																																																																																end
																																																																																																																																																																end
																																																																																																																																																																end
																																																																																																																																																																-- can't get to them (or taking too long)? pick someone else
																																																																																																																																																																if bot.victim and now - (bot.victimSince or now) > 20 then
																																																																																																																																																																	bot.victim = nil
																																																																																																																																																																	end
																																																																																																																																																																	if not bot.victim or not ctx.inRound(bot.victim) then
																																																																																																																																																																		bot.victim = pickVictim(bot, ctx)
																																																																																																																																																																		bot.victimSince = now
																																																																																																																																																																		bot.gunReaction = nil
																																																																																																																																																																		end
																																																																																																																																																																		local victim = bot.victim
																																																																																																																																																																		local vChar = victim and victim.Character
																																																																																																																																																																		local vRoot = rootOf(vChar)
																																																																																																																																																																		if not vRoot then
																																																																																																																																																																			wander(bot, ctx)
																																																																																																																																																																			return
																																																																																																																																																																			end
																																																																																																																																																																			bot.idleUntil = nil
																																																																																																																																																																			local dist = (vRoot.Position - root.Position).Magnitude
																																																																																																																																																																			local visible = canSee(bot, vChar, true)
																																																																																																																																																																			if dist > 16 then
																																																																																																																																																																				-- stalk(knife hidden until close)
																																																																																																																																																																				if not bot.chasing then
																																																																																																																																																																					unequip(bot)
																																																																																																																																																																					end
																																																																																																																																																																					setGoal(bot, vRoot.Position, visible and dist < 25)
																																																																																																																																																																					-- throw from a distance now and then
																																																																																																																																																																					if bot.chasing and visible and dist > THROW_MIN and dist < THROW_MAX and now >= (bot.nextThrowCheck or 0) then
																																																																																																																																																																						bot.nextThrowCheck = now + 1.5
																																																																																																																																																																						if rng:NextNumber() < THROW_CHANCE and now >= (bot.slashReadyAt or 0) then
																																																																																																																																																																							equip(bot, KNIFE_NAME)
																																																																																																																																																																							face(bot, vRoot.Position)
																																																																																																																																																																							bot.slashReadyAt = now + 1.2
																																																																																																																																																																							throwKnife(bot, vChar)
																																																																																																																																																																							end
																																																																																																																																																																							end
																																																																																																																																																																							elseif not bot.chasing and now - (bot.patientSince or now) < 12 and gunWatching(bot, ctx) then
																																																																																																																																																																							-- the sheriff could see : keep acting normal and wait for a better moment(not forever)
																																																																																																																																																																							bot.patientSince = bot.patientSince or now
																																																																																																																																																																							unequip(bot)
																																																																																																																																																																							if not bot.goal then
																																																																																																																																																																								wander(bot, ctx)
																																																																																																																																																																								end
																																																																																																																																																																								if rng:NextNumber() < 0.1 then
																																																																																																																																																																									bot.victim = nil -- maybe someone else is easier
																																																																																																																																																																									end
																																																																																																																																																																							else
																																																																																																																																																																								--close: knife out and go for it
																																																																																																																																																																								bot.patientSince = nil
																																																																																																																																																																								bot.chasing = true
																																																																																																																																																																								local knife = equip(bot, KNIFE_NAME)
																																																																																																																																																																								setGoal(bot, vRoot.Position, visible)
																																																																																																																																																																								if dist <= SLASH_RANGE and now >= (bot.slashReadyAt or 0) then
																																																																																																																																																																									bot.slashReadyAt = now + SLASH_COOLDOWN
																																																																																																																																																																									face(bot, vRoot.Position)
																																																																																																																																																																									playToolSound(knife, "Swoosh")
																																																																																																																																																																									task.delay(0.25, function()
																																																																																																																																																																										local r = rootOf(bot.Character)
																																																																																																																																																																										local vr = rootOf(vChar)
																																																																																																																																																																										if r and vr and (vr.Position - r.Position).Magnitude <= SLASH_RANGE + 1.5 and alive(bot.Character) then
																																																																																																																																																																											killCharacter(vChar, knife, bot)
																																																																																																																																																																											end
																																																																																																																																																																											end)
																																																																																																																																																																									end
																																																																																																																																																																									end
																																																																																																																																																																									end

																																																																																																																																																																									local function onKill(ctx, victimChar)
																																																																																																																																																																									witnessKill(ctx, victimChar)
																																																																																																																																																																									local m = ctx.murderer
																																																																																																																																																																									if BotService.isBot(m) and m.victim and m.victim.Character == victimChar then
																																																																																																																																																																										-- the murderer bot got its kill : knife away(usually) and walk off for a bit
																																																																																																																																																																										m.victim = nil
																																																																																																																																																																										m.chasing = false
																																																																																																																																																																										if rng : NextNumber() < 0.7 then
																																																																																																																																																																											unequip(m)
																																																																																																																																																																											end
																																																																																																																																																																											m.layLowUntil = os.clock() + rng : NextNumber(3, 7)
																																																																																																																																																																											local root = rootOf(m.Character)
																																																																																																																																																																											if root then
																																																																																																																																																																												local spot = escapeSpot(ctx, root.Position, root.Position)
																																																																																																																																																																												if spot then
																																																																																																																																																																													setGoal(m, spot)
																																																																																																																																																																													end
																																																																																																																																																																													end
																																																																																																																																																																													end
																																																																																																																																																																													end

																																																																																																																																																																													-- dead bots : the body falls(ragdoll), then freezes where it lies until the round ends
																																																																																																																																																																													local function onBotDied(bot)
																																																																																																																																																																													bot.dead = true
																																																																																																																																																																													task.delay(4.6, function()
																																																																																																																																																																														local char = bot.Character
																																																																																																																																																																														if char and char.Parent then
																																																																																																																																																																															for _, d in ipairs(char:GetDescendants()) do
																																																																																																																																																																																if d:IsA("BasePart") then
																																																																																																																																																																																	d.Anchored = true
																																																																																																																																																																																	d.CanCollide = false
																																																																																																																																																																																	d.CanQuery = false
																																																																																																																																																																																	d.CanTouch = false
																																																																																																																																																																																	end
																																																																																																																																																																																	end
																																																																																																																																																																																	end
																																																																																																																																																																																	end)
																																																																																																																																																																													end

																																																																																																																																																																													function BotService.startRound(ctx)
																																																																																																																																																																													--the main round stops its brains by bumping `generation`; a competition match with ctx.stopped
																																																																																																																																																																													local myGeneration
																																																																																																																																																																													if not ctx.group then
																																																																																																																																																																														generation += 1
																																																																																																																																																																														myGeneration = generation
																																																																																																																																																																														end
																																																																																																																																																																														local function running()
																																																																																																																																																																														if ctx.group then
																																																																																																																																																																															return not ctx.stopped
																																																																																																																																																																															end
																																																																																																																																																																															return generation == myGeneration
																																																																																																																																																																															end
																																																																																																																																																																															-- deaths of anyone in the round can be witnessed
																																																																																																																																																																															ctx.deathConnections = {}
																																																																																																																																																																															for _, p in ipairs(ctx.participants) do
																																																																																																																																																																																local char = p.Character
																																																																																																																																																																																local hum = char and char:FindFirstChildOfClass("Humanoid")
																																																																																																																																																																																if hum then
																																																																																																																																																																																	table.insert(ctx.deathConnections, hum.Died:Connect(function()
																																																																																																																																																																																		onKill(ctx, char)
																																																																																																																																																																																		if BotService.isBot(p) then
																																																																																																																																																																																			onBotDied(p)
																																																																																																																																																																																			end
																																																																																																																																																																																			end))
																																																																																																																																																																																	end
																																																																																																																																																																																	end
																																																																																																																																																																																	for _, p in ipairs(ctx.participants) do
																																																																																																																																																																																		if BotService.isBot(p) and p.Character then
																																																																																																																																																																																			local bot = p
																																																																																																																																																																																			bot.ctx = ctx
																																																																																																																																																																																			bot.reaction = rng:NextNumber(REACTION_MIN, REACTION_MAX)
																																																																																																																																																																																			--brain : decides what to do ~5 times a second
																																																																																																																																																																																			task.spawn(function()
																																																																																																																																																																																				task.wait(rng:NextNumber(0, 0.5))
																																																																																																																																																																																				while running() and alive(bot.Character) do
																																																																																																																																																																																					local role = ctx.roles[bot]
																																																																																																																																																																																					-- (a competition match : the Sheriff team gunfights, the Murderer team knife - fights)
																																																																																																																																																																																					local mode = ctx.teams and (ctx.teams[bot] == "Sheriff" and "Sheriffs" or "Murderers") or ctx.mode
																																																																																																																																																																																					local ok, err = pcall(function()
																																																																																																																																																																																						if mode == "Sheriffs" then
																																																																																																																																																																																							gunfightThink(bot, ctx)
																																																																																																																																																																																							elseif mode == "Murderers" then
																																																																																																																																																																																							knifeFightThink(bot, ctx)
																																																																																																																																																																																							elseif role == "Murderer" then
																																																																																																																																																																																							murdererThink(bot, ctx)
																																																																																																																																																																																							elseif findTool(bot, GUN_NAME) then
																																																																																																																																																																																							sheriffThink(bot, ctx)
																																																																																																																																																																																						else
																																																																																																																																																																																							innocentThink(bot, ctx)
																																																																																																																																																																																							end
																																																																																																																																																																																							end)
																																																																																																																																																																																					if not ok then
																																																																																																																																																																																						warn("[Bots] " ..bot.Name .. ": " ..tostring(err))
																																																																																																																																																																																						end
																																																																																																																																																																																						task.wait(0.2)
																																																																																																																																																																																						end
																																																																																																																																																																																						if bot.Character then
																																																																																																																																																																																							stopMoving(bot)
																																																																																																																																																																																							end
																																																																																																																																																																																							end)
																																																																																																																																																																																			--legs: follows the current goal ~10 times a second
																																																																																																																																																																																			task.spawn(function()
																																																																																																																																																																																				while running() and alive(bot.Character) do
																																																																																																																																																																																					pcall(stepMovement, bot)
																																																																																																																																																																																					task.wait(0.1)
																																																																																																																																																																																					end
																																																																																																																																																																																					end)
																																																																																																																																																																																			end
																																																																																																																																																																																			end
																																																																																																																																																																																			end

																																																																																																																																																																																			-- endRound() = the main round's bots leave; endRound(ctx) = only that competition match's bots
																																																																																																																																																																																			function BotService.endRound(ctx)
																																																																																																																																																																																			local group = ctx and ctx.group or nil
																																																																																																																																																																																			if ctx and ctx.group then
																																																																																																																																																																																				ctx.stopped = true
																																																																																																																																																																																			else
																																																																																																																																																																																				generation += 1
																																																																																																																																																																																				end
																																																																																																																																																																																				local leaving = {}
																																																																																																																																																																																				for bot in pairs(allBots) do
																																																																																																																																																																																					if bot.group == group then
																																																																																																																																																																																						table.insert(leaving, bot)
																																																																																																																																																																																						end
																																																																																																																																																																																						end
																																																																																																																																																																																						for _, bot in ipairs(leaving) do
																																																																																																																																																																																							allBots[bot] = nil
																																																																																																																																																																																							if bot.ctx and bot.ctx.deathConnections then
																																																																																																																																																																																								for _, c in ipairs(bot.ctx.deathConnections) do
																																																																																																																																																																																									c:Disconnect()
																																																																																																																																																																																									end
																																																																																																																																																																																									bot.ctx.deathConnections = nil
																																																																																																																																																																																									end
																																																																																																																																																																																									if bot.Character then
																																																																																																																																																																																										byCharacter[bot.Character] = nil
																																																																																																																																																																																										bot.Character:Destroy()
																																																																																																																																																																																										bot.Character = nil
																																																																																																																																																																																										end
																																																																																																																																																																																										if bot.Backpack then
																																																																																																																																																																																											bot.Backpack : Destroy()
																																																																																																																																																																																											end
																																																																																																																																																																																											bot.Parent = nil
																																																																																																																																																																																											end
																																																																																																																																																																																											if group then
																																																																																																																																																																																												return
																																																																																																																																																																																												end
																																																																																																																																																																																												-- (anything else left in the folder that isn't a competition bot's body)
																																																																																																																																																																																												for _, child in ipairs(folder:GetChildren()) do
																																																																																																																																																																																													if not byCharacter[child] then
																																																																																																																																																																																														child : Destroy()
																																																																																																																																																																																														end
																																																																																																																																																																																														end
																																																																																																																																																																																														for _, k in ipairs(workspace : GetChildren()) do
																																																																																																																																																																																															if k.Name == "BotThrownKnife" then
																																																																																																																																																																																																k : Destroy()
																																																																																																																																																																																																end
																																																																																																																																																																																																end
																																																																																																																																																																																																end

																																																																																																																																																																																																return BotService
