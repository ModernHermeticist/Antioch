using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlayerPlatformerController : PhysicsObject 
{
	[SerializeField]
	private float jumpTakeOffSpeed = 3.0f;
	[SerializeField]
	private float maxSpeed = 3.5f;
	
	private SpriteRenderer spriteRenderer;
	private Animator animator;
	void Awake () 
	{
		spriteRenderer = GetComponent<SpriteRenderer> ();
		animator = GetComponent<Animator>();
	}
	
	// Update is called once per frame
	protected override void ComputeVelocity()
	{
		Vector2 move = Vector2.zero;

		move.x = Input.GetAxis("Horizontal");

		if (Input.GetButtonDown("Jump") && grounded)
		{
			velocity.y = jumpTakeOffSpeed;
		}
		else if (Input.GetButtonUp("Jump"))
		{
			if (velocity.y > 0)
				velocity.y = velocity.y * 0.5f;
		}


		bool flipSprite = (spriteRenderer.flipX ? (move.x > 0.01f) : (move.x < -0.01f));
		if (flipSprite)
		{
			spriteRenderer.flipX = !spriteRenderer.flipX;
		} 

		animator.SetBool("grounded", grounded);
		animator.SetFloat("velocityX", Mathf.Abs(velocity.x / maxSpeed));

		targetVelocity = move * maxSpeed;
	}
}
