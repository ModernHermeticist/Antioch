using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class InalaController : MonoBehaviour 
{
	private GameObject player;
	private Rigidbody2D rb2d;
	private Vector3 headingInalaToPlayer;
	private float distanceInalaToPlayer;
	private Vector3 directionInalaToPlayer;

	private float speed = 100.0f;
	private float offset = 2.0f;
	private float maxSpeed = 3.0f;
	
	void Awake () 
	{
		player = GameObject.Find("Player");
		rb2d = GetComponent<Rigidbody2D>();
	}
	
	
	void Update()
	{
        headingInalaToPlayer = player.transform.position - gameObject.transform.position;
		distanceInalaToPlayer = headingInalaToPlayer.magnitude;
		directionInalaToPlayer = headingInalaToPlayer / distanceInalaToPlayer;
		
		if (rb2d.velocity.x <= maxSpeed && distanceInalaToPlayer > offset && directionInalaToPlayer.x > 0)
		{
			rb2d.AddForce(Vector2.right * speed * Time.deltaTime);
		}

		else if (rb2d.velocity.x <= maxSpeed && distanceInalaToPlayer > offset && directionInalaToPlayer.x < 0)
		{
            rb2d.AddForce(Vector2.left * speed * Time.deltaTime);
		}

		else if (rb2d.velocity.x >= maxSpeed)
		{
			//rb2d.velocity.x = maxSpeed;
		}

		else
		{
			rb2d.velocity = Vector2.zero;
		}
	}


}
