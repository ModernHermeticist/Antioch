using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class AutoMoveCrate : PhysicsObject
{

	
	void Start ()
	{
		
	}
	
	
	void Update ()
	{
		targetVelocity = Vector2.left;
	}
}
