using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class InalaController : PhysicsObject 
{
	private GameObject player;

	private float speed = 3.0f;
	private float offset = 5.0f;
	
	void Awake () 
	{
		player = GameObject.Find("Player");
		rb2d = GetComponent<Rigidbody2D>();
		contactFilter.useTriggers = false;
		contactFilter.SetLayerMask(Physics2D.GetLayerCollisionMask(gameObject.layer));
		contactFilter.useLayerMask = true;
		gravityModifier = 0.0f;
	}
	
	
	protected override void ComputeVelocity()
	{
		Vector2 move = Vector2.zero;

		if (player.transform.position.x - offset > gameObject.transform.position.x)
		{
			move.x = 1;
		}
		else if (player.transform.position.x + offset > gameObject.transform.position.x)
		{
			move.x = -1;
		}

		targetVelocity = move * speed;
	}


}
